#include "chat/infra/node_store_core.h"
#include "sys/clock.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace
{

uint32_t test_millis_now()
{
    return 0;
}

class CountingBlobStore final : public chat::contacts::INodeBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        out = blob;
        return load_ok;
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        blob.assign(data, data + len);
        ++save_count;
        return save_ok;
    }

    void clearBlob() override
    {
        blob.clear();
        ++clear_count;
    }

    std::vector<uint8_t> blob;
    bool load_ok = false;
    bool save_ok = true;
    int save_count = 0;
    int clear_count = 0;
};

void volatile_node_updates_do_not_force_persist()
{
    CountingBlobStore blob;
    chat::contacts::NodeStoreCore store(blob);
    store.setAutoSaveEnabled(true);
    store.begin();

    store.upsert(0xAABBCCDD,
                 "CCDD",
                 "node-ccdd",
                 10,
                 1.0f,
                 -80.0f,
                 static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic),
                 static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
                 1,
                 42,
                 0);
    assert(blob.save_count == 1);

    chat::contacts::NodeUpdate update{};
    update.has_last_seen = true;
    update.last_seen = 20;
    update.has_snr = true;
    update.snr = 5.0f;
    update.has_rssi = true;
    update.rssi = -60.0f;
    update.has_hops_away = true;
    update.hops_away = 2;
    update.has_channel = true;
    update.channel = 1;
    store.applyUpdate(0xAABBCCDD, update);

    assert(blob.save_count == 1);
    const auto& entry = store.getEntries().front();
    assert(entry.last_seen == 20);
    assert(entry.snr == 5.0f);
    assert(entry.rssi == -60.0f);
    assert(entry.hops_away == 2);
    assert(entry.channel == 1);
}

void persistent_node_updates_still_flush()
{
    CountingBlobStore blob;
    chat::contacts::NodeStoreCore store(blob);
    store.setAutoSaveEnabled(true);
    store.begin();

    store.upsert(0x01020304,
                 "0304",
                 "node-0304",
                 10,
                 1.0f,
                 -80.0f,
                 static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic),
                 static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
                 1,
                 42,
                 0);
    assert(blob.save_count == 1);

    chat::contacts::NodeUpdate name_update{};
    name_update.long_name = "node-renamed";
    store.applyUpdate(0x01020304, name_update);
    assert(blob.save_count == 2);

    chat::contacts::NodeUpdate position_update{};
    position_update.has_position = true;
    position_update.position.valid = true;
    position_update.position.latitude_i = 266777300;
    position_update.position.longitude_i = 1072822500;
    position_update.position.has_altitude = true;
    position_update.position.altitude = 1880;
    position_update.position.timestamp = 1234;
    store.applyUpdate(0x01020304, position_update);
    assert(blob.save_count == 3);

    position_update.position.timestamp = 5678;
    store.applyUpdate(0x01020304, position_update);
    assert(blob.save_count == 3);
}

void node_store_capacity_is_limited_to_24_entries()
{
    static_assert(chat::contacts::NodeStoreCore::kMaxNodes == 24,
                  "nRF node persistence budget is sized for 24 nodes");

    CountingBlobStore blob;
    chat::contacts::NodeStoreCore store(blob);
    store.setAutoSaveEnabled(false);
    store.begin();

    for (uint32_t index = 0; index < chat::contacts::NodeStoreCore::kMaxNodes + 1; ++index)
    {
        const uint32_t node_id = 0x1000U + index;
        char short_name[10] = {};
        char long_name[32] = {};
        std::snprintf(short_name, sizeof(short_name), "%04lX", static_cast<unsigned long>(node_id & 0xFFFFU));
        std::snprintf(long_name, sizeof(long_name), "node-%02lu", static_cast<unsigned long>(index));

        store.upsert(node_id,
                     short_name,
                     long_name,
                     index + 1,
                     1.0f,
                     -80.0f,
                     static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic),
                     static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
                     1,
                     42,
                     0);
    }

    assert(store.getEntries().size() == chat::contacts::NodeStoreCore::kMaxNodes);

    bool oldest_evicted = true;
    for (const auto& entry : store.getEntries())
    {
        if (entry.node_id == 0x1000U)
        {
            oldest_evicted = false;
        }
    }
    assert(oldest_evicted);
}

} // namespace

int main()
{
    sys::set_millis_provider(test_millis_now);
    volatile_node_updates_do_not_force_persist();
    persistent_node_updates_still_flush();
    node_store_capacity_is_limited_to_24_entries();
    return 0;
}
