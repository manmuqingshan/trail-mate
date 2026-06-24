/**
 * @file node_store.cpp
 * @brief Persisted NodeInfo store shell backed by a single selected persistence backend
 */

#include "platform/esp/arduino_common/chat/infra/meshtastic/node_store.h"
#include "../../internal/blob_store_io.h"
#include "chat/infra/node_store_blob_format.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_lock.h"

#include <Arduino.h>
#include <cstdio>
#include <esp_err.h>
#include <nvs.h>
#include <string>

namespace chat
{
namespace meshtastic
{

#ifndef NODE_STORE_LOG_ENABLE
#define NODE_STORE_LOG_ENABLE 1
#endif

#if NODE_STORE_LOG_ENABLE
#define NODE_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define NODE_STORE_LOG(...)
#endif

namespace
{
constexpr TickType_t kAsyncSaveMutexWait = pdMS_TO_TICKS(20);
constexpr TickType_t kAsyncSavePollInterval = pdMS_TO_TICKS(10);
constexpr TickType_t kAsyncSaveRetryDelay = pdMS_TO_TICKS(500);
constexpr TickType_t kSdLoadWait = pdMS_TO_TICKS(250);
constexpr TickType_t kSdPersistWait = pdMS_TO_TICKS(100);
constexpr uint32_t kAsyncSaveTaskStackBytes = 5 * 1024;
constexpr UBaseType_t kAsyncSaveTaskPriority = 2;

void logNvsStats(const char* tag, const char* ns)
{
    nvs_stats_t stats{};
    esp_err_t err = nvs_get_stats(nullptr, &stats);
    if (err == ESP_OK)
    {
        NODE_STORE_LOG("[NodeStore] NVS stats(%s): used=%u free=%u total=%u namespaces=%u\n",
                       tag ? tag : "?",
                       static_cast<unsigned>(stats.used_entries),
                       static_cast<unsigned>(stats.free_entries),
                       static_cast<unsigned>(stats.total_entries),
                       static_cast<unsigned>(stats.namespace_count));
    }
    else
    {
        NODE_STORE_LOG("[NodeStore] NVS stats(%s) err=%s\n",
                       tag ? tag : "?",
                       esp_err_to_name(err));
    }

    if (ns && ns[0])
    {
        nvs_handle_t handle;
        err = nvs_open(ns, NVS_READONLY, &handle);
        if (err == ESP_OK)
        {
            NODE_STORE_LOG("[NodeStore] NVS open ns=%s ok\n", ns);
            nvs_close(handle);
        }
        else
        {
            NODE_STORE_LOG("[NodeStore] NVS open ns=%s err=%s\n", ns, esp_err_to_name(err));
        }
    }
}

bool canonicalizeNodePayload(const uint8_t* data, size_t len, uint8_t version, std::vector<uint8_t>& out)
{
    std::vector<contacts::NodeEntry> entries;
    if (!contacts::NodeStoreCore::decodeBlob(entries, data, len, version))
    {
        out.clear();
        return false;
    }

    contacts::NodeStoreCore::encodeBlob(out, entries);
    return true;
}

} // namespace

NodeStore::NodeStore()
    : core_(*this)
{
}

void NodeStore::begin()
{
    backend_ = ::platform::esp::arduino_common::storage::sd_card_ready()
                   ? StorageBackend::Sd
                   : StorageBackend::Nvs;
    NODE_STORE_LOG("[NodeStore] backend=%s\n",
                   backend_ == StorageBackend::Sd ? "sd" : "nvs");
    core_.begin();
}

void NodeStore::applyUpdate(uint32_t node_id, const contacts::NodeUpdate& update)
{
    core_.applyUpdate(node_id, update);
}

void NodeStore::upsert(uint32_t node_id, const char* short_name, const char* long_name,
                       uint32_t now_secs, float snr, float rssi, uint8_t protocol,
                       uint8_t role, uint8_t hops_away, uint8_t hw_model, uint8_t channel)
{
    NODE_STORE_LOG("[NodeStore] upsert node=%08lX ts=%lu snr=%.1f rssi=%.1f\n",
                   static_cast<unsigned long>(node_id),
                   static_cast<unsigned long>(now_secs),
                   snr,
                   rssi);
    core_.upsert(node_id, short_name, long_name, now_secs, snr, rssi,
                 protocol, role, hops_away, hw_model, channel);
}

void NodeStore::updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs)
{
    core_.updateProtocol(node_id, protocol, now_secs);
}

void NodeStore::updatePosition(uint32_t node_id, const contacts::NodePosition& position)
{
    core_.updatePosition(node_id, position);
}

bool NodeStore::remove(uint32_t node_id)
{
    const bool removed = core_.remove(node_id);
    if (removed)
    {
        NODE_STORE_LOG("[NodeStore] remove node=%08lX remaining=%u\n",
                       static_cast<unsigned long>(node_id),
                       static_cast<unsigned>(core_.getEntries().size()));
    }
    return removed;
}

const std::vector<contacts::NodeEntry>& NodeStore::getEntries() const
{
    return core_.getEntries();
}

void NodeStore::clear()
{
    core_.clear();
}

bool NodeStore::flush()
{
    const bool accepted = core_.flush();
    const bool persisted = waitForAsyncSave(pdMS_TO_TICKS(1500));
    return accepted && persisted;
}

bool NodeStore::loadBlob(std::vector<uint8_t>& out)
{
    out.clear();

    if (backend_ == StorageBackend::Sd)
    {
        const LoadResult sd_result = loadFromSd(out);
        if (sd_result == LoadResult::Loaded)
        {
            NODE_STORE_LOG("[NodeStore] load source=sd path=%s len=%u\n",
                           kPersistNodesFile,
                           static_cast<unsigned>(out.size()));
            return true;
        }
        if (sd_result == LoadResult::Busy)
        {
            NODE_STORE_LOG("[NodeStore] load source=none reason=sd_busy\n");
            return false;
        }

        std::vector<uint8_t> fallback;
        if (loadFromNvs(fallback))
        {
            NODE_STORE_LOG("[NodeStore] load source=nvs fallback=sd_miss ns=%s len=%u\n",
                           kPersistNodesNs,
                           static_cast<unsigned>(fallback.size()));
            const bool migrated = saveToSd(fallback.data(), fallback.size());
            NODE_STORE_LOG("[NodeStore] migrate source=nvs target=sd path=%s len=%u ok=%u\n",
                           kPersistNodesFile,
                           static_cast<unsigned>(fallback.size()),
                           migrated ? 1U : 0U);
            if (migrated)
            {
                clearNvs();
            }
            out.swap(fallback);
            return true;
        }
    }
    else
    {
        if (loadFromNvs(out))
        {
            NODE_STORE_LOG("[NodeStore] load source=nvs ns=%s len=%u\n",
                           kPersistNodesNs,
                           static_cast<unsigned>(out.size()));
            return true;
        }
    }

    NODE_STORE_LOG("[NodeStore] load source=none\n");
    return false;
}

bool NodeStore::saveBlob(const uint8_t* data, size_t len)
{
    return enqueueAsyncSave(data, len);
}

void NodeStore::clearBlob()
{
    if (async_save_mutex_ != nullptr &&
        xSemaphoreTake(async_save_mutex_, kAsyncSaveMutexWait) == pdTRUE)
    {
        pending_save_blob_.clear();
        async_save_pending_ = false;
        async_save_failed_ = false;
        xSemaphoreGive(async_save_mutex_);
    }

    if (::platform::esp::arduino_common::storage::sd_card_ready())
    {
        ::platform::esp::common::SharedSpiLockGuard spi_guard(kSdPersistWait, "node_store_sd");
        if (spi_guard.locked() &&
            ::platform::esp::arduino_common::storage::sd_exists(kPersistNodesFile))
        {
            ::platform::esp::arduino_common::storage::sd_remove(kPersistNodesFile);
        }
    }
    clearNvs();
}

bool NodeStore::loadFromNvs(std::vector<uint8_t>& out)
{
    out.clear();

    chat::infra::PreferencesBlobMetadata meta;
    if (!chat::infra::loadRawBlobFromPreferencesWithMetadata(kPersistNodesNs,
                                                             kPersistNodesKey,
                                                             kPersistNodesKeyVer,
                                                             kPersistNodesKeyCrc,
                                                             out,
                                                             &meta))
    {
        NODE_STORE_LOG("[NodeStore] NVS blob missing/unreadable ns=%s\n", kPersistNodesNs);
        logNvsStats("begin", kPersistNodesNs);
        return false;
    }

    NODE_STORE_LOG("[NodeStore] blob len=%u ver=%u crc=%08lX has_crc=%u\n",
                   static_cast<unsigned>(meta.len),
                   static_cast<unsigned>(meta.version),
                   static_cast<unsigned long>(meta.crc),
                   meta.has_crc ? 1U : 0U);

    if (meta.len == 0)
    {
        if (meta.has_crc || meta.has_version)
        {
            NODE_STORE_LOG("[NodeStore] stale meta detected ver=%u has_crc=%u\n",
                           static_cast<unsigned>(meta.version),
                           meta.has_crc ? 1U : 0U);
        }
        NODE_STORE_LOG("[NodeStore] NVS backup empty\n");
        return false;
    }

    if (!meta.has_version)
    {
        NODE_STORE_LOG("[NodeStore] missing version len=%u\n",
                       static_cast<unsigned>(meta.len),
                       static_cast<unsigned>(meta.len));
        out.clear();
        return false;
    }

    const size_t entry_size = contacts::nodeBlobEntrySizeForVersion(meta.version);
    if (entry_size == 0 ||
        meta.len == 0 ||
        (meta.len % entry_size) != 0 ||
        (meta.len / entry_size) > contacts::NodeStoreCore::kMaxNodes)
    {
        NODE_STORE_LOG("[NodeStore] invalid blob shape len=%u ver=%u\n",
                       static_cast<unsigned>(meta.len),
                       static_cast<unsigned>(meta.version));
        out.clear();
        return false;
    }

    if (!meta.has_crc)
    {
        NODE_STORE_LOG("[NodeStore] missing crc\n");
        out.clear();
        return false;
    }

    const uint32_t calc_crc = contacts::NodeStoreCore::computeBlobCrc(out.data(), out.size());
    if (calc_crc != meta.crc)
    {
        NODE_STORE_LOG("[NodeStore] crc mismatch stored=%08lX calc=%08lX\n",
                       static_cast<unsigned long>(meta.crc),
                       static_cast<unsigned long>(calc_crc));
        out.clear();
        return false;
    }

    std::vector<uint8_t> normalized;
    if (!canonicalizeNodePayload(out.data(), out.size(), meta.version, normalized))
    {
        NODE_STORE_LOG("[NodeStore] decode failed ver=%u len=%u\n",
                       static_cast<unsigned>(meta.version),
                       static_cast<unsigned>(out.size()));
        out.clear();
        return false;
    }

    out.swap(normalized);
    NODE_STORE_LOG("[NodeStore] loaded=%u\n",
                   static_cast<unsigned>(out.size() / contacts::NodeStoreCore::kSerializedEntrySizeV8));
    return true;
}

NodeStore::LoadResult NodeStore::loadFromSd(std::vector<uint8_t>& out) const
{
    if (!::platform::esp::arduino_common::storage::sd_card_ready())
    {
        NODE_STORE_LOG("[NodeStore] load SD skipped: card none\n");
        return LoadResult::MissingOrInvalid;
    }

    ::platform::esp::common::SharedSpiLockGuard spi_guard(kSdLoadWait, "node_store_sd");
    if (!spi_guard.locked())
    {
        NODE_STORE_LOG("[NodeStore] load SD skipped: spi busy\n");
        return LoadResult::Busy;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(kPersistNodesFile, "r"))
    {
        NODE_STORE_LOG("[NodeStore] load SD open failed path=%s\n", kPersistNodesFile);
        return LoadResult::MissingOrInvalid;
    }

    const size_t file_size = static_cast<size_t>(file.size());
    if (file_size < sizeof(contacts::NodeStoreSdHeader))
    {
        NODE_STORE_LOG("[NodeStore] load SD short file path=%s size=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(file_size));
        file.close();
        return LoadResult::MissingOrInvalid;
    }

    contacts::NodeStoreSdHeader header{};
    if (file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
    {
        NODE_STORE_LOG("[NodeStore] load SD header read failed path=%s\n", kPersistNodesFile);
        file.close();
        return LoadResult::MissingOrInvalid;
    }

    if (header.count == 0 || header.count > contacts::NodeStoreCore::kMaxNodes)
    {
        NODE_STORE_LOG("[NodeStore] load SD header invalid path=%s ver=%u count=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(header.ver),
                       static_cast<unsigned>(header.count));
        file.close();
        return LoadResult::MissingOrInvalid;
    }

    const size_t payload_len = file_size - sizeof(header);
    if (payload_len == 0)
    {
        NODE_STORE_LOG("[NodeStore] load SD empty payload path=%s ver=%u count=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(header.ver),
                       static_cast<unsigned>(header.count));
        file.close();
        return LoadResult::MissingOrInvalid;
    }

    out.resize(payload_len);
    const size_t read_bytes = file.read(out.data(), payload_len);
    file.close();

    if (read_bytes != payload_len)
    {
        NODE_STORE_LOG("[NodeStore] load SD payload read mismatch path=%s want=%u got=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(payload_len),
                       static_cast<unsigned>(read_bytes));
        out.clear();
        return LoadResult::MissingOrInvalid;
    }

    const size_t entry_size = contacts::nodeBlobEntrySizeForVersion(header.ver);
    if (entry_size == 0)
    {
        NODE_STORE_LOG("[NodeStore] load SD unsupported version path=%s ver=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(header.ver));
        out.clear();
        return LoadResult::MissingOrInvalid;
    }

    const size_t expected_len = static_cast<size_t>(header.count) * entry_size;
    if (expected_len != out.size())
    {
        NODE_STORE_LOG("[NodeStore] load SD blob size mismatch path=%s len=%u ver=%u count=%u expected=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(out.size()),
                       static_cast<unsigned>(header.ver),
                       static_cast<unsigned>(header.count),
                       static_cast<unsigned>(expected_len));
        out.clear();
        return LoadResult::MissingOrInvalid;
    }

    const uint32_t calc_crc = contacts::NodeStoreCore::computeBlobCrc(out.data(), out.size());
    if (calc_crc != header.crc)
    {
        NODE_STORE_LOG("[NodeStore] load SD crc mismatch path=%s stored=%08lX calc=%08lX\n",
                       kPersistNodesFile,
                       static_cast<unsigned long>(header.crc),
                       static_cast<unsigned long>(calc_crc));
        out.clear();
        return LoadResult::MissingOrInvalid;
    }

    std::vector<uint8_t> normalized;
    if (!canonicalizeNodePayload(out.data(), out.size(), header.ver, normalized))
    {
        NODE_STORE_LOG("[NodeStore] load SD decode failed path=%s ver=%u len=%u\n",
                       kPersistNodesFile,
                       static_cast<unsigned>(header.ver),
                       static_cast<unsigned>(out.size()));
        out.clear();
        return LoadResult::MissingOrInvalid;
    }

    out.swap(normalized);
    NODE_STORE_LOG("[NodeStore] loaded=%u (SD)\n",
                   static_cast<unsigned>(out.size() / contacts::NodeStoreCore::kSerializedEntrySizeV8));
    return LoadResult::Loaded;
}

bool NodeStore::saveToNvs(const uint8_t* data, size_t len) const
{
    chat::infra::PreferencesBlobMetadata meta;
    if (data && len > 0)
    {
        if (!contacts::isValidNodeBlobSize(len) ||
            contacts::nodeBlobEntryCount(len) > contacts::NodeStoreCore::kMaxNodes)
        {
            return false;
        }
        meta.len = len;
        meta.has_version = true;
        meta.version = contacts::NodeStoreCore::kPersistVersion;
        meta.has_crc = true;
        meta.crc = contacts::NodeStoreCore::computeBlobCrc(data, len);
    }

    const bool ok = chat::infra::saveRawBlobToPreferencesWithMetadata(kPersistNodesNs,
                                                                      kPersistNodesKey,
                                                                      kPersistNodesKeyVer,
                                                                      kPersistNodesKeyCrc,
                                                                      data,
                                                                      len,
                                                                      &meta,
                                                                      true);
    if (!ok)
    {
        NODE_STORE_LOG("[NodeStore] save failed ns=%s\n", kPersistNodesNs);
        logNvsStats("save-write", kPersistNodesNs);
    }
    return ok;
}

bool NodeStore::saveToSd(const uint8_t* data, size_t len) const
{
    if (!::platform::esp::arduino_common::storage::sd_card_ready())
    {
        return false;
    }
    if (!contacts::isValidNodeBlobSize(len) ||
        contacts::nodeBlobEntryCount(len) > contacts::NodeStoreCore::kMaxNodes)
    {
        return false;
    }

    ::platform::esp::common::SharedSpiLockGuard spi_guard(kSdPersistWait, "node_store_sd");
    if (!spi_guard.locked())
    {
        NODE_STORE_LOG("[NodeStore] save SD skipped: spi busy len=%u\n",
                       static_cast<unsigned>(len));
        return false;
    }

    const std::string temp_path = std::string(kPersistNodesFile) + ".tmp";
    if (::platform::esp::arduino_common::storage::sd_exists(temp_path.c_str()))
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(temp_path.c_str(), "w"))
    {
        return false;
    }

    contacts::NodeStoreSdHeader header = contacts::makeNodeStoreSdHeader(data, len);

    bool ok = (file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header));
    if (ok && data && len > 0)
    {
        ok = (file.write(data, len) == len);
    }

    file.close();
    if (!ok)
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }

    if (::platform::esp::arduino_common::storage::sd_exists(kPersistNodesFile))
    {
        ::platform::esp::arduino_common::storage::sd_remove(kPersistNodesFile);
    }

    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), kPersistNodesFile))
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }

    NODE_STORE_LOG("[NodeStore] save SD path=%s len=%u ver=%u count=%u\n",
                   kPersistNodesFile,
                   static_cast<unsigned>(len),
                   static_cast<unsigned>(header.ver),
                   static_cast<unsigned>(header.count));
    return true;
}

bool NodeStore::saveToBackend(const uint8_t* data, size_t len, StorageBackend backend) const
{
    if (backend == StorageBackend::Sd)
    {
        const bool ok = saveToSd(data, len);
        NODE_STORE_LOG("[NodeStore] save target=sd len=%u count=%u ok=%u\n",
                       static_cast<unsigned>(len),
                       static_cast<unsigned>(contacts::nodeBlobEntryCount(len)),
                       ok ? 1U : 0U);
        return ok;
    }

    const bool ok = saveToNvs(data, len);
    NODE_STORE_LOG("[NodeStore] save target=nvs len=%u count=%u ok=%u\n",
                   static_cast<unsigned>(len),
                   static_cast<unsigned>(contacts::nodeBlobEntryCount(len)),
                   ok ? 1U : 0U);
    return ok;
}

void NodeStore::ensureAsyncSaveWorker()
{
    if (async_save_mutex_ == nullptr)
    {
        async_save_mutex_ = xSemaphoreCreateMutex();
    }
    if (async_save_queue_ == nullptr)
    {
        async_save_queue_ = xQueueCreate(1, sizeof(uint8_t));
    }
    if (async_save_task_ == nullptr && async_save_mutex_ != nullptr && async_save_queue_ != nullptr)
    {
        BaseType_t ok = xTaskCreate(asyncSaveTaskEntry,
                                    "node_store_io",
                                    kAsyncSaveTaskStackBytes,
                                    this,
                                    kAsyncSaveTaskPriority,
                                    &async_save_task_);
        if (ok != pdPASS)
        {
            NODE_STORE_LOG("[NodeStore] async save task create failed\n");
            async_save_task_ = nullptr;
        }
    }
}

bool NodeStore::enqueueAsyncSave(const uint8_t* data, size_t len)
{
    if (len > 0 && data == nullptr)
    {
        return false;
    }
    if (!contacts::isValidNodeBlobSize(len) ||
        contacts::nodeBlobEntryCount(len) > contacts::NodeStoreCore::kMaxNodes)
    {
        return false;
    }

    ensureAsyncSaveWorker();
    if (async_save_mutex_ == nullptr || async_save_queue_ == nullptr || async_save_task_ == nullptr)
    {
        return false;
    }

    if (xSemaphoreTake(async_save_mutex_, kAsyncSaveMutexWait) != pdTRUE)
    {
        NODE_STORE_LOG("[NodeStore] async save enqueue busy len=%u\n",
                       static_cast<unsigned>(len));
        return false;
    }

    pending_save_blob_.assign(data, data + len);
    pending_save_backend_ = backend_;
    ++pending_save_generation_;
    async_save_pending_ = true;
    async_save_failed_ = false;
    const uint32_t generation = pending_save_generation_;
    const StorageBackend target = pending_save_backend_;
    xSemaphoreGive(async_save_mutex_);

    const uint8_t signal = 1;
    if (xQueueOverwrite(async_save_queue_, &signal) != pdTRUE)
    {
        NODE_STORE_LOG("[NodeStore] async save signal failed gen=%lu\n",
                       static_cast<unsigned long>(generation));
        return false;
    }

    NODE_STORE_LOG("[NodeStore] save queued gen=%lu target=%s len=%u count=%u\n",
                   static_cast<unsigned long>(generation),
                   target == StorageBackend::Sd ? "sd" : "nvs",
                   static_cast<unsigned>(len),
                   static_cast<unsigned>(contacts::nodeBlobEntryCount(len)));
    return true;
}

bool NodeStore::waitForAsyncSave(TickType_t wait_ticks)
{
    ensureAsyncSaveWorker();
    if (async_save_mutex_ == nullptr || async_save_queue_ == nullptr || async_save_task_ == nullptr)
    {
        return false;
    }

    const TickType_t start = xTaskGetTickCount();
    while (true)
    {
        bool done = false;
        bool failed = false;
        if (xSemaphoreTake(async_save_mutex_, kAsyncSaveMutexWait) == pdTRUE)
        {
            done = !async_save_pending_ && !async_save_busy_;
            failed = async_save_failed_;
            xSemaphoreGive(async_save_mutex_);
        }
        if (done)
        {
            return !failed;
        }
        if ((xTaskGetTickCount() - start) >= wait_ticks)
        {
            return false;
        }
        vTaskDelay(kAsyncSavePollInterval);
    }
}

void NodeStore::asyncSaveLoop()
{
    uint8_t signal = 0;
    for (;;)
    {
        if (xQueueReceive(async_save_queue_, &signal, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        for (;;)
        {
            std::vector<uint8_t> blob;
            StorageBackend backend = StorageBackend::Nvs;
            uint32_t generation = 0;

            if (xSemaphoreTake(async_save_mutex_, portMAX_DELAY) != pdTRUE)
            {
                break;
            }
            if (!async_save_pending_)
            {
                async_save_busy_ = false;
                xSemaphoreGive(async_save_mutex_);
                break;
            }
            blob = pending_save_blob_;
            backend = pending_save_backend_;
            generation = pending_save_generation_;
            async_save_pending_ = false;
            async_save_busy_ = true;
            xSemaphoreGive(async_save_mutex_);

            const bool ok = saveToBackend(blob.data(), blob.size(), backend);

            bool has_more = false;
            if (xSemaphoreTake(async_save_mutex_, portMAX_DELAY) == pdTRUE)
            {
                async_save_busy_ = false;
                async_save_failed_ = !ok;
                if (ok)
                {
                    completed_save_generation_ = generation;
                }
                else if (!async_save_pending_)
                {
                    pending_save_blob_ = blob;
                    pending_save_backend_ = backend;
                    async_save_pending_ = true;
                }
                has_more = async_save_pending_;
                xSemaphoreGive(async_save_mutex_);
            }

            NODE_STORE_LOG("[NodeStore] save done gen=%lu ok=%u more=%u\n",
                           static_cast<unsigned long>(generation),
                           ok ? 1U : 0U,
                           has_more ? 1U : 0U);
            if (!ok)
            {
                vTaskDelay(kAsyncSaveRetryDelay);
            }
            if (!has_more)
            {
                break;
            }
        }
    }
}

void NodeStore::asyncSaveTaskEntry(void* context)
{
    auto* self = static_cast<NodeStore*>(context);
    if (self)
    {
        self->asyncSaveLoop();
    }
    vTaskDelete(nullptr);
}

void NodeStore::clearNvs() const
{
    chat::infra::clearPreferencesKeys(kPersistNodesNs,
                                      kPersistNodesKey,
                                      kPersistNodesKeyVer,
                                      kPersistNodesKeyCrc);
}

} // namespace meshtastic
} // namespace chat
