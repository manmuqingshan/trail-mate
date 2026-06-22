/**
 * @file node_store.h
 * @brief Persisted NodeInfo store shell backed by a single selected persistence backend
 */

#pragma once

#include "chat/infra/node_store_core.h"
#include "chat/ports/i_node_blob_store.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <vector>

namespace chat
{
namespace meshtastic
{

class NodeStore : public contacts::INodeStore,
                  private contacts::INodeBlobStore
{
  public:
    NodeStore();

    void begin() override;
    void applyUpdate(uint32_t node_id, const contacts::NodeUpdate& update) override;
    void upsert(uint32_t node_id, const char* short_name, const char* long_name,
                uint32_t now_secs, float snr = 0.0f, float rssi = 0.0f, uint8_t protocol = 0,
                uint8_t role = contacts::kNodeRoleUnknown, uint8_t hops_away = 0xFF,
                uint8_t hw_model = 0, uint8_t channel = 0xFF) override;
    void updateProtocol(uint32_t node_id, uint8_t protocol, uint32_t now_secs) override;
    void updatePosition(uint32_t node_id, const contacts::NodePosition& position) override;
    bool remove(uint32_t node_id) override;
    const std::vector<contacts::NodeEntry>& getEntries() const override;
    void clear() override;
    bool flush() override;

  private:
    enum class StorageBackend : uint8_t
    {
        Sd,
        Nvs,
    };

    enum class LoadResult : uint8_t
    {
        Loaded,
        MissingOrInvalid,
        Busy,
    };

    static constexpr const char* kPersistNodesFile = "/nodes.bin";
    static constexpr const char* kPersistNodesNs = "nodes";
    static constexpr const char* kPersistNodesKey = "node_blob";
    static constexpr const char* kPersistNodesKeyVer = "ver";
    static constexpr const char* kPersistNodesKeyCrc = "crc";

    bool loadBlob(std::vector<uint8_t>& out) override;
    bool saveBlob(const uint8_t* data, size_t len) override;
    void clearBlob() override;

    bool loadFromNvs(std::vector<uint8_t>& out);
    LoadResult loadFromSd(std::vector<uint8_t>& out) const;
    bool saveToNvs(const uint8_t* data, size_t len) const;
    bool saveToSd(const uint8_t* data, size_t len) const;
    bool saveToBackend(const uint8_t* data, size_t len, StorageBackend backend) const;
    void ensureAsyncSaveWorker();
    bool enqueueAsyncSave(const uint8_t* data, size_t len);
    bool waitForAsyncSave(TickType_t wait_ticks);
    void asyncSaveLoop();
    static void asyncSaveTaskEntry(void* context);
    void clearNvs() const;

    contacts::NodeStoreCore core_;
    StorageBackend backend_ = StorageBackend::Nvs;
    SemaphoreHandle_t async_save_mutex_ = nullptr;
    QueueHandle_t async_save_queue_ = nullptr;
    TaskHandle_t async_save_task_ = nullptr;
    std::vector<uint8_t> pending_save_blob_;
    StorageBackend pending_save_backend_ = StorageBackend::Nvs;
    uint32_t pending_save_generation_ = 0;
    uint32_t completed_save_generation_ = 0;
    bool async_save_pending_ = false;
    bool async_save_busy_ = false;
    bool async_save_failed_ = false;
};

} // namespace meshtastic
} // namespace chat
