#pragma once

#include <cstdint>

#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "sdmmc_cmd.h"

namespace platform::esp::idf_common::sdmmc_host_runtime
{

enum class SlotOwner : uint8_t
{
    None = 0,
    SdCard,
    C6Companion,
    UsbMassStorage,
};

struct Snapshot
{
    SlotOwner slot0_owner = SlotOwner::None;
    SlotOwner slot1_owner = SlotOwner::None;
    uint8_t active_slot_count = 0;
    uint8_t host_ref_count = 0;
    uint8_t sd_card_host_refs = 0;
    uint8_t c6_companion_host_refs = 0;
    uint8_t usb_mass_storage_host_refs = 0;
};

/**
 * Initializes one non-FATFS SDMMC/SDIO slot under an explicit logical owner.
 * The matching release_slot() delegates to sdmmc_host_deinit_slot() for the
 * physical slot while this runtime owns the logical Host refcount. A failed or
 * restarting C6 owner must never release the SD card owner's Host reference.
 */
esp_err_t initialize_slot(SlotOwner owner,
                          const sdmmc_host_t& host,
                          const sdmmc_slot_config_t& slot_config);

esp_err_t release_slot(SlotOwner owner, int slot);

Snapshot snapshot();
const char* owner_name(SlotOwner owner);

} // namespace platform::esp::idf_common::sdmmc_host_runtime
