#include "chat/infra/mesh_peer_directory_core.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

namespace
{

chat::NodeId reticulum_node_id_from_destination_hash(const uint8_t* destination_hash)
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<chat::NodeId>(destination_hash[12]) << 24) |
           (static_cast<chat::NodeId>(destination_hash[13]) << 16) |
           (static_cast<chat::NodeId>(destination_hash[14]) << 8) |
           static_cast<chat::NodeId>(destination_hash[15]);
}

class MemoryMeshPeerDirectory final : public chat::IMeshPeerDirectory
{
  public:
    chat::MeshPeerDirectoryStatus begin() override
    {
        begun_ = true;
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryStatus record(
        const chat::MeshPeerRecord& record) override
    {
        if (!begun_ || !chat::meshPeerRecordIsValid(record))
        {
            return chat::MeshPeerDirectoryStatus::fail(
                chat::MeshPeerDirectoryStatusCode::InvalidArgument);
        }

        for (auto& existing : records_)
        {
            if (chat::sameMeshPeerIdentity(existing.identity, record.identity))
            {
                const uint32_t first_seen = existing.first_seen_s;
                existing = record;
                existing.first_seen_s =
                    first_seen != 0 ? first_seen : record.first_seen_s;
                if (record.last_seen_s < existing.first_seen_s)
                {
                    existing.last_seen_s = existing.first_seen_s;
                }
                return chat::MeshPeerDirectoryStatus::success();
            }
        }

        const auto capacity = capacityFor(record.identity.protocol);
        const std::size_t current_count =
            countForProtocol(record.identity.protocol);
        if (capacity.persisted_records != 0 &&
            current_count >= capacity.persisted_records)
        {
            evictOldest(record.identity.protocol);
        }

        records_.push_back(record);
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryStatus find(
        const chat::MeshPeerIdentity& identity,
        chat::MeshPeerRecord& out_record) override
    {
        for (const auto& record : records_)
        {
            if (chat::sameMeshPeerIdentity(record.identity, identity))
            {
                out_record = record;
                return chat::MeshPeerDirectoryStatus::success();
            }
        }
        return chat::MeshPeerDirectoryStatus::fail(
            chat::MeshPeerDirectoryStatusCode::NotFound);
    }

    chat::MeshPeerDirectoryStatus findByNodeId(
        chat::MeshProtocol protocol,
        chat::NodeId node_id,
        chat::MeshPeerRecord& out_record) override
    {
        if (node_id == 0)
        {
            return chat::MeshPeerDirectoryStatus::fail(
                chat::MeshPeerDirectoryStatusCode::InvalidArgument);
        }

        for (const auto& record : records_)
        {
            if (!chat::meshPeerSameProtocol(record.identity.protocol, protocol))
            {
                continue;
            }
            if (record.identity.kind == chat::MeshPeerIdentityKind::NodeId &&
                record.identity.node_id == node_id)
            {
                out_record = record;
                return chat::MeshPeerDirectoryStatus::success();
            }
            if (chat::meshPeerIsReticulumProtocol(protocol) &&
                record.identity.kind ==
                    chat::MeshPeerIdentityKind::ReticulumDestination &&
                record.identity.reticulum.valid &&
                reticulum_node_id_from_destination_hash(
                    record.identity.reticulum.destination_hash) == node_id)
            {
                out_record = record;
                return chat::MeshPeerDirectoryStatus::success();
            }
        }
        return chat::MeshPeerDirectoryStatus::fail(
            chat::MeshPeerDirectoryStatusCode::NotFound);
    }

    chat::MeshPeerDirectoryStatus loadRecent(
        chat::MeshProtocol protocol,
        chat::MeshPeerRecord* out_records,
        std::size_t max_records,
        std::size_t* out_count) override
    {
        if (!out_count || (!out_records && max_records > 0))
        {
            return chat::MeshPeerDirectoryStatus::fail(
                chat::MeshPeerDirectoryStatusCode::InvalidArgument);
        }

        std::vector<chat::MeshPeerRecord> matches;
        for (const auto& record : records_)
        {
            if (chat::meshPeerSameProtocol(record.identity.protocol, protocol))
            {
                matches.push_back(record);
            }
        }
        std::sort(matches.begin(),
                  matches.end(),
                  [](const chat::MeshPeerRecord& lhs,
                     const chat::MeshPeerRecord& rhs)
                  {
                      return lhs.last_seen_s > rhs.last_seen_s;
                  });

        const std::size_t count = std::min(max_records, matches.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            out_records[index] = matches[index];
        }
        *out_count = count;
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryStatus search(
        chat::MeshProtocol protocol,
        const char* query,
        chat::MeshPeerRecord* out_records,
        std::size_t max_records,
        std::size_t* out_count) override
    {
        if (!query || !out_count || (!out_records && max_records > 0))
        {
            return chat::MeshPeerDirectoryStatus::fail(
                chat::MeshPeerDirectoryStatusCode::InvalidArgument);
        }

        *out_count = 0;
        for (const auto& record : records_)
        {
            if (!chat::meshPeerSameProtocol(record.identity.protocol, protocol))
            {
                continue;
            }
            if (std::strstr(record.display_name, query) == nullptr)
            {
                continue;
            }
            if (*out_count < max_records)
            {
                out_records[*out_count] = record;
            }
            ++(*out_count);
            if (*out_count >= max_records)
            {
                break;
            }
        }
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryStatus setUserFlags(
        const chat::MeshPeerIdentity& identity,
        const chat::MeshPeerUserFlags& flags) override
    {
        for (auto& record : records_)
        {
            if (chat::sameMeshPeerIdentity(record.identity, identity))
            {
                record.flags = flags;
                return chat::MeshPeerDirectoryStatus::success();
            }
        }
        return chat::MeshPeerDirectoryStatus::fail(
            chat::MeshPeerDirectoryStatusCode::NotFound);
    }

    chat::MeshPeerDirectoryStatus remove(
        const chat::MeshPeerIdentity& identity) override
    {
        for (auto it = records_.begin(); it != records_.end(); ++it)
        {
            if (chat::sameMeshPeerIdentity(it->identity, identity))
            {
                records_.erase(it);
                return chat::MeshPeerDirectoryStatus::success();
            }
        }
        return chat::MeshPeerDirectoryStatus::fail(
            chat::MeshPeerDirectoryStatusCode::NotFound);
    }

    chat::MeshPeerDirectoryStatus clearProtocol(
        chat::MeshProtocol protocol) override
    {
        records_.erase(std::remove_if(records_.begin(),
                                      records_.end(),
                                      [protocol](const chat::MeshPeerRecord& record)
                                      {
                                          return chat::meshPeerSameProtocol(
                                              record.identity.protocol,
                                              protocol);
                                      }),
                       records_.end());
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryCapacity capacityFor(
        chat::MeshProtocol protocol) const override
    {
        if (protocol == chat::MeshProtocol::Meshtastic)
        {
            return meshtastic_capacity;
        }
        if (protocol == chat::MeshProtocol::MeshCore)
        {
            return meshcore_capacity;
        }
        if (chat::meshPeerIsReticulumProtocol(protocol))
        {
            return reticulum_capacity;
        }
        return {};
    }

    chat::MeshPeerDirectoryStatus flush() override
    {
        return chat::MeshPeerDirectoryStatus::success();
    }

    chat::MeshPeerDirectoryCapacity meshtastic_capacity{2, 1};
    chat::MeshPeerDirectoryCapacity meshcore_capacity{1, 1};
    chat::MeshPeerDirectoryCapacity reticulum_capacity{3, 1};

  private:
    std::size_t countForProtocol(chat::MeshProtocol protocol) const
    {
        std::size_t count = 0;
        for (const auto& record : records_)
        {
            if (chat::meshPeerSameProtocol(record.identity.protocol, protocol))
            {
                ++count;
            }
        }
        return count;
    }

    void evictOldest(chat::MeshProtocol protocol)
    {
        auto oldest = records_.end();
        for (auto it = records_.begin(); it != records_.end(); ++it)
        {
            if (!chat::meshPeerSameProtocol(it->identity.protocol, protocol))
            {
                continue;
            }
            if (oldest == records_.end() ||
                it->last_seen_s < oldest->last_seen_s)
            {
                oldest = it;
            }
        }
        if (oldest != records_.end())
        {
            records_.erase(oldest);
        }
    }

    bool begun_ = false;
    std::vector<chat::MeshPeerRecord> records_;
};

class CountingMeshPeerDirectoryBlobStore final
    : public chat::IMeshPeerDirectoryBlobStore
{
  public:
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(
        std::vector<uint8_t>& out) override
    {
        if (unavailable)
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Unavailable;
        }
        if (!has_blob)
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Missing;
        }
        out = blob;
        ++load_count;
        return chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }

    bool saveBlob(const uint8_t* data, std::size_t len) override
    {
        if (unavailable || (!data && len > 0))
        {
            return false;
        }
        blob.assign(data, data + len);
        has_blob = true;
        ++save_count;
        return true;
    }

    void clearBlob() override
    {
        blob.clear();
        has_blob = false;
        ++clear_count;
    }

    std::vector<uint8_t> blob;
    bool has_blob = false;
    bool unavailable = false;
    int load_count = 0;
    int save_count = 0;
    int clear_count = 0;
};

chat::ReticulumPeerIdentity makeReticulumIdentity(uint8_t seed)
{
    uint8_t destination[chat::kReticulumPeerHashSize] = {};
    uint8_t identity[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        destination[index] = static_cast<uint8_t>(seed + index);
        identity[index] = static_cast<uint8_t>(seed + 0x40 + index);
    }
    return chat::makeReticulumPeerIdentity(destination, identity);
}

chat::MeshPeerRecord makeMeshtasticPeer(uint32_t node_id,
                                        const char* name,
                                        uint32_t seen_s)
{
    chat::MeshPeerRecord record{};
    record.valid = true;
    record.identity =
        chat::makeMeshPeerNodeIdentity(chat::MeshProtocol::Meshtastic, node_id);
    record.source = chat::MeshPeerSource::RuntimeRx;
    record.first_seen_s = seen_s;
    record.last_seen_s = seen_s;
    chat::copyMeshPeerText(record.display_name,
                           sizeof(record.display_name),
                           name);
    record.meshtastic.has_public_key = true;
    for (std::size_t index = 0;
         index < chat::kMeshPeerMeshtasticPublicKeyLen;
         ++index)
    {
        record.meshtastic.public_key[index] =
            static_cast<uint8_t>(node_id + index);
    }
    record.meshtastic.node.role = 1;
    return record;
}

chat::MeshPeerRecord makeMeshCorePeer(uint8_t seed,
                                      const char* name,
                                      uint32_t seen_s)
{
    uint8_t public_key[chat::kMeshPeerMeshCorePublicKeyLen] = {};
    for (std::size_t index = 0; index < sizeof(public_key); ++index)
    {
        public_key[index] = static_cast<uint8_t>(seed + index);
    }

    chat::MeshPeerRecord record{};
    record.valid = true;
    assert(chat::makeMeshPeerPublicKeyIdentity(chat::MeshProtocol::MeshCore,
                                               public_key,
                                               sizeof(public_key),
                                               record.identity));
    record.source = chat::MeshPeerSource::DiscoveryResponse;
    record.first_seen_s = seen_s;
    record.last_seen_s = seen_s;
    chat::copyMeshPeerText(record.display_name,
                           sizeof(record.display_name),
                           name);
    record.meshcore.has_public_key = true;
    std::memcpy(record.meshcore.public_key, public_key, sizeof(public_key));
    record.meshcore.public_key_verified = true;
    record.meshcore.has_peer_hash = true;
    record.meshcore.peer_hash = seed;
    return record;
}

chat::MeshPeerRecord makeReticulumPeer(uint8_t seed,
                                       const char* name,
                                       uint32_t seen_s)
{
    chat::MeshPeerRecord record{};
    record.valid = true;
    record.reticulum.identity = makeReticulumIdentity(seed);
    record.identity =
        chat::makeMeshPeerReticulumIdentity(record.reticulum.identity);
    record.source = chat::MeshPeerSource::RuntimeRx;
    record.first_seen_s = seen_s;
    record.last_seen_s = seen_s;
    chat::copyMeshPeerText(record.display_name,
                           sizeof(record.display_name),
                           name);
    record.reticulum.has_public_keys = true;
    for (std::size_t index = 0;
         index < chat::kMeshPeerReticulumPublicKeyLen;
         ++index)
    {
        record.reticulum.enc_pub[index] = static_cast<uint8_t>(seed + index);
        record.reticulum.sig_pub[index] =
            static_cast<uint8_t>(seed + 0x20 + index);
    }
    record.reticulum.has_ratchet = true;
    for (std::size_t index = 0;
         index < chat::kMeshPeerReticulumRatchetLen;
         ++index)
    {
        record.reticulum.ratchet_pub[index] =
            static_cast<uint8_t>(seed + 0x60 + index);
    }
    record.reticulum.ratchet_seen_s = seen_s;
    record.reticulum.delivery = true;
    return record;
}

void upsert_preserves_first_seen_and_updates_peer_facts()
{
    MemoryMeshPeerDirectory directory;
    assert(directory.begin().succeeded());

    auto original = makeMeshtasticPeer(0x0C16AAEC, "alpha", 10);
    assert(directory.record(original).succeeded());

    auto updated = makeMeshtasticPeer(0x0C16AAEC, "alpha new", 25);
    updated.first_seen_s = 25;
    updated.meshtastic.node.role = 2;
    updated.meshtastic.key_manually_verified = true;
    updated.meshtastic.public_key[0] = 0xAA;
    assert(directory.record(updated).succeeded());

    chat::MeshPeerRecord loaded{};
    assert(directory.find(original.identity, loaded).succeeded());
    assert(loaded.first_seen_s == 10);
    assert(loaded.last_seen_s == 25);
    assert(std::strcmp(loaded.display_name, "alpha new") == 0);
    assert(loaded.meshtastic.node.role == 2);
    assert(loaded.meshtastic.key_manually_verified);
    assert(loaded.meshtastic.public_key[0] == 0xAA);
}

void protocol_identity_shapes_do_not_collapse()
{
    MemoryMeshPeerDirectory directory;
    assert(directory.begin().succeeded());

    auto meshtastic = makeMeshtasticPeer(0x01020304, "mt peer", 10);
    auto meshcore = makeMeshCorePeer(0x11, "mc peer", 11);
    auto reticulum = makeReticulumPeer(0x21, "rt peer", 12);

    assert(directory.record(meshtastic).succeeded());
    assert(directory.record(meshcore).succeeded());
    assert(directory.record(reticulum).succeeded());

    chat::MeshPeerRecord loaded{};
    assert(directory.find(meshtastic.identity, loaded).succeeded());
    assert(loaded.identity.kind == chat::MeshPeerIdentityKind::NodeId);
    assert(loaded.meshtastic.has_public_key);

    assert(directory.find(meshcore.identity, loaded).succeeded());
    assert(loaded.identity.kind == chat::MeshPeerIdentityKind::PublicKey);
    assert(loaded.meshcore.public_key_verified);

    assert(directory.find(reticulum.identity, loaded).succeeded());
    assert(loaded.identity.kind ==
           chat::MeshPeerIdentityKind::ReticulumDestination);
    assert(loaded.reticulum.has_public_keys);
    assert(loaded.reticulum.has_ratchet);
    assert(loaded.reticulum.delivery);
}

void core_persists_reticulum_ratchet()
{
    CountingMeshPeerDirectoryBlobStore blob;
    chat::MeshPeerDirectoryCore::Options options{};
    options.auto_save = true;
    const auto original = makeReticulumPeer(0x71, "ratchet peer", 55);

    {
        chat::MeshPeerDirectoryCore directory(blob, options);
        assert(directory.begin().succeeded());
        assert(directory.record(original).succeeded());
    }

    {
        chat::MeshPeerDirectoryCore directory(blob, options);
        assert(directory.begin().succeeded());
        chat::MeshPeerRecord loaded{};
        assert(directory.find(original.identity, loaded).succeeded());
        assert(loaded.reticulum.has_ratchet);
        assert(loaded.reticulum.ratchet_seen_s == 55);
        assert(std::memcmp(loaded.reticulum.ratchet_pub,
                           original.reticulum.ratchet_pub,
                           chat::kMeshPeerReticulumRatchetLen) == 0);
    }
}

void capacity_is_protocol_policy_not_interface_shape()
{
    MemoryMeshPeerDirectory directory;
    assert(directory.begin().succeeded());

    assert(directory.record(makeMeshtasticPeer(1, "mt one", 1)).succeeded());
    assert(directory.record(makeMeshtasticPeer(2, "mt two", 2)).succeeded());
    assert(directory.record(makeMeshtasticPeer(3, "mt three", 3)).succeeded());

    assert(directory.record(makeReticulumPeer(0x30, "rt one", 1)).succeeded());
    assert(directory.record(makeReticulumPeer(0x40, "rt two", 2)).succeeded());
    assert(directory.record(makeReticulumPeer(0x50, "rt three", 3)).succeeded());

    chat::MeshPeerRecord records[4]{};
    std::size_t count = 0;
    assert(directory
               .loadRecent(chat::MeshProtocol::Meshtastic,
                           records,
                           4,
                           &count)
               .succeeded());
    assert(count == 2);
    assert(records[0].identity.node_id == 3);
    assert(records[1].identity.node_id == 2);

    assert(directory
               .loadRecent(chat::MeshProtocol::Reticulum,
                           records,
                           4,
                           &count)
               .succeeded());
    assert(count == 3);
    assert(records[0].reticulum.identity.destination_hash[0] == 0x50);
    assert(records[2].reticulum.identity.destination_hash[0] == 0x30);

    const auto mt_capacity =
        directory.capacityFor(chat::MeshProtocol::Meshtastic);
    const auto rt_capacity =
        directory.capacityFor(chat::MeshProtocol::Reticulum);
    assert(mt_capacity.persisted_records == 2);
    assert(rt_capacity.persisted_records == 3);
}

void search_and_user_flags_are_directory_behaviors()
{
    MemoryMeshPeerDirectory directory;
    assert(directory.begin().succeeded());

    auto beta = makeReticulumPeer(0x60, "beta trail", 1);
    assert(directory.record(makeReticulumPeer(0x61, "alpha trail", 2))
               .succeeded());
    assert(directory.record(beta).succeeded());

    chat::MeshPeerRecord records[1]{};
    std::size_t count = 0;
    assert(directory
               .search(chat::MeshProtocol::Reticulum,
                       "beta",
                       records,
                       1,
                       &count)
               .succeeded());
    assert(count == 1);
    assert(std::strcmp(records[0].display_name, "beta trail") == 0);

    chat::MeshPeerUserFlags flags{};
    flags.favorite = true;
    flags.trusted = true;
    assert(directory.setUserFlags(beta.identity, flags).succeeded());

    chat::MeshPeerRecord loaded{};
    assert(directory.find(beta.identity, loaded).succeeded());
    assert(loaded.flags.favorite);
    assert(loaded.flags.trusted);
    assert(!loaded.flags.ignored);
}

void find_by_node_id_preserves_reticulum_destination_identity()
{
    MemoryMeshPeerDirectory memory_directory;
    assert(memory_directory.begin().succeeded());

    auto reticulum = makeReticulumPeer(0x63, "rt node", 44);
    const chat::NodeId node_id =
        reticulum_node_id_from_destination_hash(
            reticulum.identity.reticulum.destination_hash);
    assert(node_id != 0);
    assert(memory_directory.record(reticulum).succeeded());

    chat::MeshPeerRecord loaded{};
    assert(memory_directory
               .findByNodeId(chat::MeshProtocol::RNode, node_id, loaded)
               .succeeded());
    assert(chat::sameReticulumDestinationHash(loaded.identity.reticulum,
                                              reticulum.identity.reticulum));

    CountingMeshPeerDirectoryBlobStore blob;
    chat::MeshPeerDirectoryCore core_directory(blob);
    assert(core_directory.begin().succeeded());
    assert(core_directory.record(reticulum).succeeded());
    loaded = chat::MeshPeerRecord{};
    assert(core_directory
               .findByNodeId(chat::MeshProtocol::Reticulum, node_id, loaded)
               .succeeded());
    assert(chat::sameReticulumDestinationHash(loaded.identity.reticulum,
                                              reticulum.identity.reticulum));
}

void core_persists_records_and_preserves_user_flags()
{
    CountingMeshPeerDirectoryBlobStore blob;
    chat::MeshPeerDirectoryCore::Options options{};
    options.meshtastic_capacity = {4, 2};
    options.meshcore_capacity = {4, 2};
    options.reticulum_capacity = {4, 2};
    options.auto_save = true;

    chat::MeshPeerIdentity alpha_identity{};
    {
        chat::MeshPeerDirectoryCore directory(blob, options);
        assert(directory.begin().succeeded());

        auto alpha = makeMeshtasticPeer(0x0C16AAEC, "alpha", 10);
        alpha_identity = alpha.identity;
        assert(directory.record(alpha).succeeded());

        chat::MeshPeerUserFlags flags{};
        flags.favorite = true;
        flags.trusted = true;
        assert(directory.setUserFlags(alpha_identity, flags).succeeded());

        auto updated = makeMeshtasticPeer(0x0C16AAEC, "alpha fresh", 30);
        assert(directory.record(updated).succeeded());
        assert(blob.save_count >= 3);
    }

    {
        chat::MeshPeerDirectoryCore directory(blob, options);
        assert(directory.begin().succeeded());
        assert(directory.count(chat::MeshProtocol::Meshtastic) == 1);

        chat::MeshPeerRecord loaded{};
        assert(directory.find(alpha_identity, loaded).succeeded());
        assert(loaded.first_seen_s == 10);
        assert(loaded.last_seen_s == 30);
        assert(std::strcmp(loaded.display_name, "alpha fresh") == 0);
        assert(loaded.flags.favorite);
        assert(loaded.flags.trusted);
    }
}

void remove_and_clear_protocol_are_directory_behaviors()
{
    MemoryMeshPeerDirectory directory;
    assert(directory.begin().succeeded());

    auto meshtastic = makeMeshtasticPeer(0x01020304, "mt", 1);
    auto meshcore = makeMeshCorePeer(0x12, "mc", 2);
    assert(directory.record(meshtastic).succeeded());
    assert(directory.record(meshcore).succeeded());

    assert(directory.remove(meshtastic.identity).succeeded());
    chat::MeshPeerRecord loaded{};
    assert(!directory.find(meshtastic.identity, loaded).succeeded());
    assert(directory.find(meshcore.identity, loaded).succeeded());

    assert(directory.clearProtocol(chat::MeshProtocol::MeshCore).succeeded());
    assert(!directory.find(meshcore.identity, loaded).succeeded());
}

} // namespace

int main()
{
    upsert_preserves_first_seen_and_updates_peer_facts();
    protocol_identity_shapes_do_not_collapse();
    core_persists_reticulum_ratchet();
    capacity_is_protocol_policy_not_interface_shape();
    search_and_user_flags_are_directory_behaviors();
    find_by_node_id_preserves_reticulum_destination_identity();
    core_persists_records_and_preserves_user_flags();
    remove_and_clear_protocol_are_directory_behaviors();
    return 0;
}
