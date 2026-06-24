#include "chat/infra/node_store_core.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace
{

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

} // namespace

int main()
{
    volatile_node_updates_do_not_force_persist();
    persistent_node_updates_still_flush();
    return 0;
}
