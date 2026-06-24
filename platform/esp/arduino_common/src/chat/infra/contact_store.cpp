/**
 * @file contact_store.cpp
 * @brief Contact nickname storage shell implementation
 */

#include "platform/esp/arduino_common/chat/infra/contact_store.h"
#include "../internal/blob_store_io.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_lock.h"

#include <Arduino.h>

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

namespace
{
constexpr TickType_t kSdLoadWait = pdMS_TO_TICKS(250);
constexpr TickType_t kSdPersistWait = pdMS_TO_TICKS(100);
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
    ::platform::esp::common::SharedSpiLockGuard spi_guard(kSdLoadWait, "contact_store_sd");
    if (!spi_guard.locked())
    {
        out.clear();
        return LoadResult::Busy;
    }
    return chat::infra::loadRawBlobFromSd(kSdPath, out)
               ? LoadResult::Loaded
               : LoadResult::MissingOrInvalid;
}

bool ContactStore::saveToSD(const uint8_t* data, size_t len) const
{
    ::platform::esp::common::SharedSpiLockGuard spi_guard(kSdPersistWait, "contact_store_sd");
    if (!spi_guard.locked())
    {
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
