/**
 * @file contact_store.cpp
 * @brief Contact nickname storage shell implementation
 */

#include "platform/esp/arduino_common/chat/infra/contact_store.h"
#include "../internal/blob_store_io.h"
#include "platform/esp/arduino_common/storage/persistence_bus_gate.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_bus_arbiter.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <new>

namespace chat
{
namespace contacts
{

#ifndef CONTACT_STORE_LOG_ENABLE
#define CONTACT_STORE_LOG_ENABLE 1
#endif

#if CONTACT_STORE_LOG_ENABLE
#define CONTACT_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CONTACT_STORE_LOG(...)
#endif

void* ContactStore::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void ContactStore::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void ContactStore::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}

namespace
{
constexpr uint32_t kSdLoadWaitMs = 250;
constexpr uint32_t kSdPersistWaitMs = 750;
constexpr uint32_t kContactStoreBusResource = 5;
constexpr uint32_t kContactStoreBusOwnerId = 0x434F4E54u; // 'CONT'
constexpr const char* kContactStoreBusOwner = "contact_store_sd";

::platform::esp::common::SharedSpiBusAdapter s_contact_store_bus_adapter(
    kContactStoreBusOwner,
    kContactStoreBusOwnerId);
::platform::esp::common::FixedSharedSpiBusPolicyStrategy s_contact_store_bus_policy(
    kSdLoadWaitMs,
    kSdLoadWaitMs,
    kSdLoadWaitMs,
    kSdLoadWaitMs);
sys::runtime::StorageBusArbiter s_contact_store_bus_arbiter(
    s_contact_store_bus_adapter,
    s_contact_store_bus_policy);
} // namespace

ContactStore::ContactStore()
    : core_(*this)
{
}

void ContactStore::begin()
{
    backend_ = ::platform::esp::arduino_common::storage::sd_card_ready()
                   ? StorageBackend::Sd
                   : StorageBackend::Flash;
    CONTACT_STORE_LOG("[ContactStore] backend=%s\n",
                      backend_ == StorageBackend::Sd ? "sd" : "flash");
    core_.begin();
}

std::string ContactStore::getNickname(uint32_t node_id) const
{
    return core_.getNickname(node_id);
}

bool ContactStore::setNickname(uint32_t node_id, const char* nickname)
{
    return core_.setNickname(node_id, nickname);
}

bool ContactStore::removeNickname(uint32_t node_id)
{
    return core_.removeNickname(node_id);
}

bool ContactStore::hasNickname(const char* nickname) const
{
    return core_.hasNickname(nickname);
}

std::vector<uint32_t> ContactStore::getAllContactIds() const
{
    return core_.getAllContactIds();
}

size_t ContactStore::getCount() const
{
    return core_.getCount();
}

bool ContactStore::loadBlob(std::vector<uint8_t>& out)
{
    if (backend_ == StorageBackend::Sd)
    {
        const LoadResult sd_result = loadFromSD(out);
        if (sd_result == LoadResult::Loaded)
        {
            CONTACT_STORE_LOG("[ContactStore] load source=sd path=%s len=%u\n",
                              kSdPath,
                              static_cast<unsigned>(out.size()));
            return true;
        }
        if (sd_result == LoadResult::Busy)
        {
            CONTACT_STORE_LOG("[ContactStore] load source=none reason=sd_busy\n");
            out.clear();
            return false;
        }

        std::vector<uint8_t> fallback;
        if (loadFromFlash(fallback))
        {
            CONTACT_STORE_LOG("[ContactStore] load source=flash fallback=sd_miss ns=%s len=%u\n",
                              kPrefNs,
                              static_cast<unsigned>(fallback.size()));
            const bool migrated = saveToSD(fallback.data(), fallback.size());
            CONTACT_STORE_LOG("[ContactStore] migrate source=flash target=sd path=%s len=%u ok=%u\n",
                              kSdPath,
                              static_cast<unsigned>(fallback.size()),
                              migrated ? 1U : 0U);
            if (migrated)
            {
                clearFlash();
            }
            out.swap(fallback);
            return true;
        }
    }
    else
    {
        if (loadFromFlash(out))
        {
            CONTACT_STORE_LOG("[ContactStore] load source=flash ns=%s len=%u\n",
                              kPrefNs,
                              static_cast<unsigned>(out.size()));
            return true;
        }
    }
    out.clear();
    CONTACT_STORE_LOG("[ContactStore] load source=none\n");
    return false;
}

bool ContactStore::saveBlob(const uint8_t* data, size_t len)
{
    if (backend_ == StorageBackend::Sd)
    {
        const bool ok = saveToSD(data, len);
        CONTACT_STORE_LOG("[ContactStore] save target=sd path=%s len=%u ok=%u\n",
                          kSdPath,
                          static_cast<unsigned>(len),
                          ok ? 1U : 0U);
        return ok;
    }

    const bool ok = saveToFlash(data, len);
    CONTACT_STORE_LOG("[ContactStore] save target=flash ns=%s len=%u ok=%u\n",
                      kPrefNs,
                      static_cast<unsigned>(len),
                      ok ? 1U : 0U);
    return ok;
}

ContactStore::LoadResult ContactStore::loadFromSD(std::vector<uint8_t>& out) const
{
    if (!::platform::esp::arduino_common::storage::sd_card_ready())
    {
        out.clear();
        return LoadResult::MissingOrInvalid;
    }
    ::platform::esp::arduino_common::storage::PersistenceBusGate bus_gate(
        s_contact_store_bus_arbiter,
        sys::runtime::BusAccessPolicy::BackgroundWorkerBounded,
        kSdLoadWaitMs,
        kContactStoreBusResource,
        kContactStoreBusOwnerId + 1,
        kContactStoreBusOwnerId);
    if (!bus_gate.locked())
    {
        out.clear();
        return LoadResult::Busy;
    }
    constexpr size_t kMaxContactBlobBytes =
        ContactStoreCore::kMaxContacts * ContactStoreCore::kSerializedEntrySize;
    return chat::infra::loadRawBlobFromSd(kSdPath, out, kMaxContactBlobBytes)
               ? LoadResult::Loaded
               : LoadResult::MissingOrInvalid;
}

bool ContactStore::saveToSD(const uint8_t* data, size_t len) const
{
    ::platform::esp::arduino_common::storage::PersistenceBusGate bus_gate(
        s_contact_store_bus_arbiter,
        sys::runtime::BusAccessPolicy::DurableCommit,
        kSdPersistWaitMs,
        kContactStoreBusResource,
        kContactStoreBusOwnerId + 2,
        kContactStoreBusOwnerId);
    if (!bus_gate.locked())
    {
        CONTACT_STORE_LOG("[ContactStore] save SD skipped: spi busy len=%u\n",
                          static_cast<unsigned>(len));
        return false;
    }
    return chat::infra::saveRawBlobToSd(kSdPath, data, len);
}

bool ContactStore::loadFromFlash(std::vector<uint8_t>& out) const
{
    return chat::infra::loadRawBlobFromPreferences(kPrefNs, kPrefKey, out);
}

bool ContactStore::saveToFlash(const uint8_t* data, size_t len) const { return chat::infra::saveRawBlobToPreferences(kPrefNs, kPrefKey, data, len); }

void ContactStore::clearFlash() const
{
    chat::infra::clearRawBlobFromPreferences(kPrefNs, kPrefKey);
}

} // namespace contacts
} // namespace chat
