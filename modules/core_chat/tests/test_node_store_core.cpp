#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_core.h"
#include "chat/usecase/contact_service.h"
#include "sys/clock.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

class CountingContactBlobStore final : public chat::IContactBlobStore
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

    std::vector<uint8_t> blob;
    bool load_ok = false;
    bool save_ok = true;
    int save_count = 0;
};

class EmptyContactStore final : public chat::contacts::IContactStore
{
  public:
    void begin() override {}
    std::string getNickname(uint32_t node_id) const override
    {
        (void)node_id;
        return std::string();
    }
    bool setNickname(uint32_t node_id, const char* nickname) override
    {
        (void)node_id;
        (void)nickname;
        return false;
    }
    bool removeNickname(uint32_t node_id) override
    {
        (void)node_id;
        return false;
    }
    bool hasNickname(const char* nickname) const override
    {
        (void)nickname;
        return false;
    }
    std::vector<uint32_t> getAllContactIds() const override
    {
        return {};
    }
    size_t getCount() const override
    {
        return 0;
    }
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

void reticulum_identity_updates_are_persisted_and_lookupable()
{
    CountingBlobStore blob;
    chat::contacts::NodeStoreCore store(blob);
    store.setAutoSaveEnabled(true);
    store.begin();

    store.upsert(0xC001D00D,
                 "D00D",
                 "reticulum-peer",
                 10,
                 1.0f,
                 -80.0f,
                 static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum),
                 static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
                 1,
                 0,
                 0xFF);
    assert(blob.save_count == 1);

    chat::contacts::NodeUpdate update{};
    uint8_t destination_hash[chat::contacts::kReticulumPeerHashSize] = {};
    uint8_t identity_hash[chat::contacts::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::contacts::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] = static_cast<uint8_t>(0x10U + index);
        identity_hash[index] = static_cast<uint8_t>(0x40U + index);
    }
    update.reticulum_identity =
        chat::makeReticulumPeerIdentity(destination_hash, identity_hash);

    store.applyUpdate(0xC001D00D, update);

    assert(blob.save_count == 2);
    assert(blob.blob.size() == chat::contacts::NodeStoreCore::kSerializedEntrySize);
    const auto& entry = store.getEntries().front();
    assert(entry.reticulum_identity.valid);
    assert(std::memcmp(entry.reticulum_identity.destination_hash,
                       update.reticulum_identity.destination_hash,
                       chat::contacts::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(entry.reticulum_identity.identity_hash,
                       update.reticulum_identity.identity_hash,
                       chat::contacts::kReticulumPeerHashSize) == 0);

    CountingBlobStore persisted_blob;
    persisted_blob.load_ok = true;
    persisted_blob.blob = blob.blob;
    chat::contacts::NodeStoreCore restored_store(persisted_blob);
    restored_store.begin();
    const auto& restored_entry = restored_store.getEntries().front();
    assert(restored_entry.reticulum_identity.valid);
    assert(std::memcmp(restored_entry.reticulum_identity.destination_hash,
                       update.reticulum_identity.destination_hash,
                       chat::contacts::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(restored_entry.reticulum_identity.identity_hash,
                       update.reticulum_identity.identity_hash,
                       chat::contacts::kReticulumPeerHashSize) == 0);

    EmptyContactStore contact_store;
    chat::contacts::ContactService contact_service(restored_store, contact_store);
    uint32_t projected_node_id = 0;
    assert(contact_service.findNodeIdByReticulumDestinationHash(
        update.reticulum_identity.destination_hash,
        &projected_node_id));
    assert(projected_node_id == 0xC001D00D);
}

void v8_node_blobs_decode_without_reticulum_identity()
{
    CountingBlobStore blob;
    chat::contacts::NodeStoreCore store(blob);
    store.setAutoSaveEnabled(true);
    store.begin();

    store.upsert(0x0102A0B0,
                 "A0B0",
                 "legacy-node",
                 10,
                 1.0f,
                 -80.0f,
                 static_cast<uint8_t>(chat::contacts::NodeProtocolType::Meshtastic),
                 static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
                 1,
                 42,
                 0);
    assert(blob.blob.size() == chat::contacts::NodeStoreCore::kSerializedEntrySize);

    std::vector<uint8_t> legacy_blob(blob.blob.begin(),
                                     blob.blob.begin() +
                                         chat::contacts::NodeStoreCore::kSerializedEntrySizeV8);
    std::vector<chat::contacts::NodeEntry> decoded;
    assert(chat::contacts::NodeStoreCore::decodeBlob(decoded,
                                                     legacy_blob.data(),
                                                     legacy_blob.size(),
                                                     chat::contacts::NodeStoreCore::kPersistVersionV8));
    assert(decoded.size() == 1);
    assert(decoded.front().node_id == 0x0102A0B0);
    assert(!decoded.front().reticulum_identity.valid);
}

void node_store_capacity_is_limited_to_200_entries()
{
    static_assert(chat::contacts::NodeStoreCore::kMaxNodes == 200,
                  "Node persistence budget is sized for 200 nodes");

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

void contact_store_allows_reapplying_and_duplicate_display_names()
{
    CountingContactBlobStore blob;
    chat::contacts::ContactStoreCore store(blob);
    store.begin();

    assert(store.setNickname(0x01020304U, "Unknown Peer"));
    assert(blob.save_count == 1);
    assert(store.setNickname(0x01020304U, "Unknown Peer"));
    assert(blob.save_count == 1);
    assert(store.setNickname(0x05060708U, "Unknown Peer"));
    assert(blob.save_count == 2);
    assert(store.getCount() == 2);
    assert(store.getNickname(0x01020304U) == "Unknown Peer");
    assert(store.getNickname(0x05060708U) == "Unknown Peer");
}

void contact_store_rolls_back_failed_saves()
{
    CountingContactBlobStore blob;
    chat::contacts::ContactStoreCore store(blob);
    store.begin();

    blob.save_ok = false;
    assert(!store.setNickname(0x01020304U, "RT-01020304"));
    assert(store.getCount() == 0);
    assert(store.getNickname(0x01020304U).empty());

    blob.save_ok = true;
    assert(store.setNickname(0x01020304U, "RT-01020304"));
    assert(store.getCount() == 1);
    assert(store.getNickname(0x01020304U) == "RT-01020304");

    blob.save_ok = false;
    assert(!store.setNickname(0x01020304U, "RT-05060708"));
    assert(store.getCount() == 1);
    assert(store.getNickname(0x01020304U) == "RT-01020304");
}

} // namespace

int main()
{
    sys::set_millis_provider(test_millis_now);
    volatile_node_updates_do_not_force_persist();
    persistent_node_updates_still_flush();
    reticulum_identity_updates_are_persisted_and_lookupable();
    v8_node_blobs_decode_without_reticulum_identity();
    node_store_capacity_is_limited_to_200_entries();
    contact_store_allows_reapplying_and_duplicate_display_names();
    contact_store_rolls_back_failed_saves();
    return 0;
}
