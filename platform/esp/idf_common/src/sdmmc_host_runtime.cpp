#include "platform/esp/idf_common/sdmmc_host_runtime.h"

#include <array>
#include <mutex>

#include "esp_log.h"

namespace platform::esp::idf_common::sdmmc_host_runtime
{
namespace
{

constexpr const char* kTag = "idf-sdmmc-owner";
constexpr size_t kSlotCount = 2;

std::mutex s_lifecycle_mutex;
std::array<SlotOwner, kSlotCount> s_slot_owners{SlotOwner::None, SlotOwner::None};
std::array<uint8_t, 4> s_host_owner_refs{};
uint8_t s_host_ref_count = 0;

bool valid_owner(SlotOwner owner)
{
    return owner != SlotOwner::None;
}

bool valid_slot(int slot)
{
    return slot >= 0 && static_cast<size_t>(slot) < s_slot_owners.size();
}

uint8_t active_slot_count_locked()
{
    uint8_t count = 0;
    for (const SlotOwner owner : s_slot_owners)
    {
        if (owner != SlotOwner::None)
        {
            ++count;
        }
    }
    return count;
}

size_t owner_ref_index(SlotOwner owner)
{
    return static_cast<size_t>(owner);
}

uint8_t owner_ref_count_locked(SlotOwner owner)
{
    const size_t index = owner_ref_index(owner);
    return index < s_host_owner_refs.size() ? s_host_owner_refs[index] : 0;
}

esp_err_t ensure_slot_available_locked(SlotOwner owner, int slot)
{
    if (!valid_owner(owner) || !valid_slot(slot))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const SlotOwner current = s_slot_owners[static_cast<size_t>(slot)];
    if (current == SlotOwner::None)
    {
        return ESP_OK;
    }

    ESP_LOGW(kTag,
             "SDMMC slot busy slot=%d requester=%s owner=%s active_slots=%u host_refs=%u",
             slot,
             owner_name(owner),
             owner_name(current),
             static_cast<unsigned>(active_slot_count_locked()),
             static_cast<unsigned>(s_host_ref_count));
    return ESP_ERR_INVALID_STATE;
}

void record_acquired_slot_locked(SlotOwner owner, int slot)
{
    s_slot_owners[static_cast<size_t>(slot)] = owner;
    const size_t owner_index = owner_ref_index(owner);
    if (owner_index < s_host_owner_refs.size())
    {
        ++s_host_owner_refs[owner_index];
    }
    ++s_host_ref_count;
    ESP_LOGI(kTag,
             "SDMMC slot acquired slot=%d owner=%s active_slots=%u host_refs=%u owner_refs=%u",
             slot,
             owner_name(owner),
             static_cast<unsigned>(active_slot_count_locked()),
             static_cast<unsigned>(s_host_ref_count),
             static_cast<unsigned>(owner_ref_count_locked(owner)));
}

void record_released_slot_locked(SlotOwner owner, int slot, const char* driver_state)
{
    s_slot_owners[static_cast<size_t>(slot)] = SlotOwner::None;
    const size_t owner_index = owner_ref_index(owner);
    if (owner_index < s_host_owner_refs.size() && s_host_owner_refs[owner_index] > 0)
    {
        --s_host_owner_refs[owner_index];
    }
    if (s_host_ref_count > 0)
    {
        --s_host_ref_count;
    }
    ESP_LOGI(kTag,
             "SDMMC slot released slot=%d owner=%s active_slots=%u host_refs=%u owner_refs=%u driver_state=%s",
             slot,
             owner_name(owner),
             static_cast<unsigned>(active_slot_count_locked()),
             static_cast<unsigned>(s_host_ref_count),
             static_cast<unsigned>(owner_ref_count_locked(owner)),
             driver_state ? driver_state : "unknown");
}

} // namespace

const char* owner_name(SlotOwner owner)
{
    switch (owner)
    {
    case SlotOwner::SdCard:
        return "sd-card";
    case SlotOwner::C6Companion:
        return "c6-companion";
    case SlotOwner::UsbMassStorage:
        return "usb-msc";
    case SlotOwner::None:
        break;
    }
    return "none";
}

esp_err_t initialize_slot(SlotOwner owner,
                          const sdmmc_host_t& host,
                          const sdmmc_slot_config_t& slot_config)
{
    if (host.init == nullptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_lifecycle_mutex);
    const esp_err_t availability = ensure_slot_available_locked(owner, host.slot);
    if (availability != ESP_OK)
    {
        return availability;
    }

    esp_err_t err = (*host.init)();
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "SDMMC Host init failed requester=%s slot=%d err=%s",
                 owner_name(owner),
                 host.slot,
                 esp_err_to_name(err));
        return err;
    }

    err = sdmmc_host_init_slot(host.slot, &slot_config);
    if (err != ESP_OK)
    {
        // Do not call forceful sdmmc_host_deinit() here: another slot may be
        // active in ESP-IDF even if it predates this logical owner registry.
        ESP_LOGW(kTag,
                 "SDMMC slot init failed requester=%s slot=%d err=%s active_slots=%u host_refs=%u",
                 owner_name(owner),
                 host.slot,
                 esp_err_to_name(err),
                 static_cast<unsigned>(active_slot_count_locked()),
                 static_cast<unsigned>(s_host_ref_count));
        return err;
    }

    record_acquired_slot_locked(owner, host.slot);
    return ESP_OK;
}

esp_err_t release_slot(SlotOwner owner, int slot)
{
    if (!valid_owner(owner) || !valid_slot(slot))
    {
        return ESP_ERR_INVALID_ARG;
    }

    std::lock_guard<std::mutex> lock(s_lifecycle_mutex);
    const size_t index = static_cast<size_t>(slot);
    const SlotOwner current = s_slot_owners[index];
    if (current == SlotOwner::None)
    {
        return ESP_OK;
    }
    if (current != owner)
    {
        ESP_LOGW(kTag,
                 "SDMMC slot release denied slot=%d requester=%s owner=%s",
                 slot,
                 owner_name(owner),
                 owner_name(current));
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t err = sdmmc_host_deinit_slot(slot);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(kTag,
                 "SDMMC slot release failed slot=%d owner=%s err=%s",
                 slot,
                 owner_name(owner),
                 esp_err_to_name(err));
        return err;
    }

    record_released_slot_locked(owner, slot, esp_err_to_name(err));
    return err;
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(s_lifecycle_mutex);
    return Snapshot{
        s_slot_owners[0],
        s_slot_owners[1],
        active_slot_count_locked(),
        s_host_ref_count,
        owner_ref_count_locked(SlotOwner::SdCard),
        owner_ref_count_locked(SlotOwner::C6Companion),
        owner_ref_count_locked(SlotOwner::UsbMassStorage),
    };
}

} // namespace platform::esp::idf_common::sdmmc_host_runtime
