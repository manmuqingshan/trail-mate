#include "chat/infra/mesh_peer_directory_core.h"

#include <algorithm>
#include <cstring>

#if defined(_MSC_VER)
#define TRAILMATE_PACK_PUSH __pragma(pack(push, 1))
#define TRAILMATE_PACK_POP __pragma(pack(pop))
#define TRAILMATE_PACKED
#else
#define TRAILMATE_PACK_PUSH
#define TRAILMATE_PACK_POP
#define TRAILMATE_PACKED __attribute__((packed))
#endif

namespace chat
{

namespace
{

constexpr uint32_t kMeshPeerDirectoryMagic = 0x5244504DUL; // MPDR
constexpr uint8_t kMeshPeerDirectoryPersistVersion = 1;

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerDirectoryHeaderV1
{
    uint32_t magic = kMeshPeerDirectoryMagic;
    uint8_t version = kMeshPeerDirectoryPersistVersion;
    uint8_t reserved[3] = {};
    uint32_t count = 0;
    uint32_t crc = 0;
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerNodeFactsV1
{
    char short_name[kMeshPeerShortNameMaxLen] = {};
    char long_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t role = 0xFF;
    uint8_t hw_model = 0;
    uint8_t channel = 0xFF;
    uint8_t hops_away = 0xFF;
    uint8_t has_macaddr = 0;
    uint8_t via_mqtt = 0;
    uint8_t reserved[2] = {};
    uint8_t macaddr[kMeshPeerMacAddrLen] = {};
    uint8_t reserved_mac[2] = {};
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

TRAILMATE_PACK_PUSH
struct PersistedMeshPeerEntryV1
{
    uint8_t valid = 0;
    uint8_t protocol = 0;
    uint8_t identity_kind = 0;
    uint8_t source = 0;
    uint32_t node_id = 0;
    uint8_t identity_public_key_len = 0;
    uint8_t reserved_identity[3] = {};
    uint8_t identity_public_key[kMeshPeerPublicKeyMaxLen] = {};
    uint8_t identity_reticulum_valid = 0;
    uint8_t reserved_reticulum_identity[3] = {};
    uint8_t reticulum_destination_hash[kReticulumPeerHashSize] = {};
    uint8_t reticulum_identity_hash[kReticulumPeerHashSize] = {};
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    char display_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t favorite = 0;
    uint8_t ignored = 0;
    uint8_t trusted = 0;
    uint8_t reserved_flags = 0;

    PersistedMeshPeerNodeFactsV1 meshtastic_node{};
    uint8_t meshtastic_has_public_key = 0;
    uint8_t meshtastic_key_verified = 0;
    uint8_t meshtastic_reserved[2] = {};
    uint8_t meshtastic_public_key[kMeshPeerMeshtasticPublicKeyLen] = {};

    PersistedMeshPeerNodeFactsV1 meshcore_node{};
    uint8_t meshcore_has_public_key = 0;
    uint8_t meshcore_key_verified = 0;
    uint8_t meshcore_has_peer_hash = 0;
    uint8_t meshcore_peer_hash = 0;
    uint8_t meshcore_has_next_hop = 0;
    uint8_t meshcore_next_hop = 0;
    uint8_t meshcore_reserved[2] = {};
    uint8_t meshcore_public_key[kMeshPeerMeshCorePublicKeyLen] = {};

    uint8_t reticulum_identity_valid = 0;
    uint8_t reticulum_has_public_keys = 0;
    uint8_t reticulum_delivery = 0;
    uint8_t reticulum_propagation = 0;
    uint8_t reticulum_destination_hash2[kReticulumPeerHashSize] = {};
    uint8_t reticulum_identity_hash2[kReticulumPeerHashSize] = {};
    uint8_t reticulum_enc_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t reticulum_sig_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t reticulum_has_ratchet = 0;
    uint8_t reticulum_reserved[3] = {};
    uint32_t reticulum_ratchet_seen_s = 0;
    uint8_t reticulum_ratchet_pub[kMeshPeerReticulumRatchetLen] = {};
} TRAILMATE_PACKED;
TRAILMATE_PACK_POP

void copyIntoPersisted(PersistedMeshPeerNodeFactsV1& dst,
                       const MeshPeerNodeFacts& src)
{
    dst = {};
    std::memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    std::memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.role = src.role;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
    dst.hops_away = src.hops_away;
    dst.has_macaddr = src.has_macaddr ? 1U : 0U;
    dst.via_mqtt = src.via_mqtt ? 1U : 0U;
    std::memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
}

void copyFromPersisted(MeshPeerNodeFacts& dst,
                       const PersistedMeshPeerNodeFactsV1& src)
{
    dst = {};
    std::memcpy(dst.short_name, src.short_name, sizeof(dst.short_name));
    dst.short_name[sizeof(dst.short_name) - 1] = '\0';
    std::memcpy(dst.long_name, src.long_name, sizeof(dst.long_name));
    dst.long_name[sizeof(dst.long_name) - 1] = '\0';
    dst.role = src.role;
    dst.hw_model = src.hw_model;
    dst.channel = src.channel;
    dst.hops_away = src.hops_away;
    dst.has_macaddr = src.has_macaddr != 0;
    dst.via_mqtt = src.via_mqtt != 0;
    std::memcpy(dst.macaddr, src.macaddr, sizeof(dst.macaddr));
}

void copyReticulumIdentityBytes(ReticulumPeerIdentity& dst,
                                uint8_t valid,
                                const uint8_t destination[kReticulumPeerHashSize],
                                const uint8_t identity[kReticulumPeerHashSize])
{
    dst = {};
    if (valid == 0)
    {
        return;
    }
    dst = makeReticulumPeerIdentity(destination, identity);
}

void copyIntoPersisted(PersistedMeshPeerEntryV1& dst,
                       const MeshPeerRecord& src)
{
    dst = {};
    dst.valid = src.valid ? 1U : 0U;
    dst.protocol = static_cast<uint8_t>(src.identity.protocol);
    dst.identity_kind = static_cast<uint8_t>(src.identity.kind);
    dst.source = static_cast<uint8_t>(src.source);
    dst.node_id = src.identity.node_id;
    dst.identity_public_key_len = src.identity.public_key_len;
    std::memcpy(dst.identity_public_key,
                src.identity.public_key,
                sizeof(dst.identity_public_key));
    dst.identity_reticulum_valid = src.identity.reticulum.valid ? 1U : 0U;
    (void)copyReticulumIdentityHashes(dst.reticulum_destination_hash,
                                      dst.reticulum_identity_hash,
                                      src.identity.reticulum);
    dst.first_seen_s = src.first_seen_s;
    dst.last_seen_s = src.last_seen_s;
    std::memcpy(dst.display_name, src.display_name, sizeof(dst.display_name));
    dst.display_name[sizeof(dst.display_name) - 1] = '\0';
    dst.favorite = src.flags.favorite ? 1U : 0U;
    dst.ignored = src.flags.ignored ? 1U : 0U;
    dst.trusted = src.flags.trusted ? 1U : 0U;

    copyIntoPersisted(dst.meshtastic_node, src.meshtastic.node);
    dst.meshtastic_has_public_key = src.meshtastic.has_public_key ? 1U : 0U;
    dst.meshtastic_key_verified =
        src.meshtastic.key_manually_verified ? 1U : 0U;
    std::memcpy(dst.meshtastic_public_key,
                src.meshtastic.public_key,
                sizeof(dst.meshtastic_public_key));

    copyIntoPersisted(dst.meshcore_node, src.meshcore.node);
    dst.meshcore_has_public_key = src.meshcore.has_public_key ? 1U : 0U;
    dst.meshcore_key_verified = src.meshcore.public_key_verified ? 1U : 0U;
    dst.meshcore_has_peer_hash = src.meshcore.has_peer_hash ? 1U : 0U;
    dst.meshcore_peer_hash = src.meshcore.peer_hash;
    dst.meshcore_has_next_hop = src.meshcore.has_next_hop ? 1U : 0U;
    dst.meshcore_next_hop = src.meshcore.next_hop;
    std::memcpy(dst.meshcore_public_key,
                src.meshcore.public_key,
                sizeof(dst.meshcore_public_key));

    dst.reticulum_identity_valid = src.reticulum.identity.valid ? 1U : 0U;
    dst.reticulum_has_public_keys = src.reticulum.has_public_keys ? 1U : 0U;
    dst.reticulum_delivery = src.reticulum.delivery ? 1U : 0U;
    dst.reticulum_propagation = src.reticulum.propagation ? 1U : 0U;
    (void)copyReticulumIdentityHashes(dst.reticulum_destination_hash2,
                                      dst.reticulum_identity_hash2,
                                      src.reticulum.identity);
    std::memcpy(dst.reticulum_enc_pub,
                src.reticulum.enc_pub,
                sizeof(dst.reticulum_enc_pub));
    std::memcpy(dst.reticulum_sig_pub,
                src.reticulum.sig_pub,
                sizeof(dst.reticulum_sig_pub));
    dst.reticulum_has_ratchet = src.reticulum.has_ratchet ? 1U : 0U;
    dst.reticulum_ratchet_seen_s = src.reticulum.ratchet_seen_s;
    std::memcpy(dst.reticulum_ratchet_pub,
                src.reticulum.ratchet_pub,
                sizeof(dst.reticulum_ratchet_pub));
}

bool copyFromPersisted(MeshPeerRecord& dst,
                       const PersistedMeshPeerEntryV1& src)
{
    dst = {};
    dst.valid = src.valid != 0;
    dst.identity.protocol = static_cast<MeshProtocol>(src.protocol);
    dst.identity.kind = static_cast<MeshPeerIdentityKind>(src.identity_kind);
    dst.identity.node_id = src.node_id;
    dst.identity.public_key_len = src.identity_public_key_len;
    if (dst.identity.public_key_len > kMeshPeerPublicKeyMaxLen)
    {
        return false;
    }
    std::memcpy(dst.identity.public_key,
                src.identity_public_key,
                sizeof(dst.identity.public_key));
    copyReticulumIdentityBytes(dst.identity.reticulum,
                               src.identity_reticulum_valid,
                               src.reticulum_destination_hash,
                               src.reticulum_identity_hash);
    dst.source = static_cast<MeshPeerSource>(src.source);
    dst.first_seen_s = src.first_seen_s;
    dst.last_seen_s = src.last_seen_s;
    std::memcpy(dst.display_name, src.display_name, sizeof(dst.display_name));
    dst.display_name[sizeof(dst.display_name) - 1] = '\0';
    dst.flags.favorite = src.favorite != 0;
    dst.flags.ignored = src.ignored != 0;
    dst.flags.trusted = src.trusted != 0;

    copyFromPersisted(dst.meshtastic.node, src.meshtastic_node);
    dst.meshtastic.has_public_key = src.meshtastic_has_public_key != 0;
    dst.meshtastic.key_manually_verified = src.meshtastic_key_verified != 0;
    std::memcpy(dst.meshtastic.public_key,
                src.meshtastic_public_key,
                sizeof(dst.meshtastic.public_key));

    copyFromPersisted(dst.meshcore.node, src.meshcore_node);
    dst.meshcore.has_public_key = src.meshcore_has_public_key != 0;
    dst.meshcore.public_key_verified = src.meshcore_key_verified != 0;
    dst.meshcore.has_peer_hash = src.meshcore_has_peer_hash != 0;
    dst.meshcore.peer_hash = src.meshcore_peer_hash;
    dst.meshcore.has_next_hop = src.meshcore_has_next_hop != 0;
    dst.meshcore.next_hop = src.meshcore_next_hop;
    std::memcpy(dst.meshcore.public_key,
                src.meshcore_public_key,
                sizeof(dst.meshcore.public_key));

    copyReticulumIdentityBytes(dst.reticulum.identity,
                               src.reticulum_identity_valid,
                               src.reticulum_destination_hash2,
                               src.reticulum_identity_hash2);
    dst.reticulum.has_public_keys = src.reticulum_has_public_keys != 0;
    dst.reticulum.delivery = src.reticulum_delivery != 0;
    dst.reticulum.propagation = src.reticulum_propagation != 0;
    std::memcpy(dst.reticulum.enc_pub,
                src.reticulum_enc_pub,
                sizeof(dst.reticulum.enc_pub));
    std::memcpy(dst.reticulum.sig_pub,
                src.reticulum_sig_pub,
                sizeof(dst.reticulum.sig_pub));
    dst.reticulum.has_ratchet =
        src.reticulum_has_ratchet != 0 &&
        meshPeerHasNonZeroBytes(src.reticulum_ratchet_pub,
                                sizeof(src.reticulum_ratchet_pub));
    if (dst.reticulum.has_ratchet)
    {
        std::memcpy(dst.reticulum.ratchet_pub,
                    src.reticulum_ratchet_pub,
                    sizeof(dst.reticulum.ratchet_pub));
        dst.reticulum.ratchet_seen_s = src.reticulum_ratchet_seen_s;
    }
    return meshPeerRecordIsValid(dst);
}

bool textContains(const char* haystack, const char* needle)
{
    return haystack && needle && std::strstr(haystack, needle) != nullptr;
}

NodeId reticulumNodeIdFromDestinationHash(const uint8_t* destination_hash)
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<NodeId>(destination_hash[12]) << 24) |
           (static_cast<NodeId>(destination_hash[13]) << 16) |
           (static_cast<NodeId>(destination_hash[14]) << 8) |
           static_cast<NodeId>(destination_hash[15]);
}

} // namespace

MeshPeerDirectoryCore::MeshPeerDirectoryCore(
    IMeshPeerDirectoryBlobStore& blob_store)
    : blob_store_(blob_store)
{
}

MeshPeerDirectoryCore::MeshPeerDirectoryCore(
    IMeshPeerDirectoryBlobStore& blob_store,
    const Options& options)
    : blob_store_(blob_store), options_(options)
{
}

void MeshPeerDirectoryCore::setAutoSaveEnabled(bool enabled)
{
    options_.auto_save = enabled;
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::begin()
{
    std::vector<uint8_t> blob;
    const auto loaded = blob_store_.loadBlob(blob);
    if (loaded == MeshPeerDirectoryBlobLoadResult::Unavailable)
    {
        begun_ = false;
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    if (loaded == MeshPeerDirectoryBlobLoadResult::IoError)
    {
        begun_ = false;
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }

    records_.clear();
    dirty_ = false;
    begun_ = true;
    if (loaded == MeshPeerDirectoryBlobLoadResult::Missing || blob.empty())
    {
        return MeshPeerDirectoryStatus::success();
    }
    if (!decodeBlob(records_, blob.data(), blob.size()))
    {
        records_.clear();
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::record(
    const MeshPeerRecord& record)
{
    if (!begun_ || !meshPeerRecordIsValid(record))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    const std::size_t existing_index = findIndex(record.identity);
    if (existing_index < records_.size())
    {
        MeshPeerRecord next = record;
        const MeshPeerRecord& existing = records_[existing_index];
        next.first_seen_s =
            existing.first_seen_s != 0 ? existing.first_seen_s : record.first_seen_s;
        if (next.last_seen_s < next.first_seen_s)
        {
            next.last_seen_s = next.first_seen_s;
        }
        next.flags = existing.flags;
        records_[existing_index] = next;
        dirty_ = true;
        maybeSave();
        return MeshPeerDirectoryStatus::success();
    }

    const auto capacity = capacityFor(record.identity.protocol);
    if (capacity.persisted_records != 0 &&
        countForProtocol(record.identity.protocol) >= capacity.persisted_records)
    {
        evictOldest(record.identity.protocol);
    }

    MeshPeerRecord next = record;
    if (next.last_seen_s < next.first_seen_s)
    {
        next.last_seen_s = next.first_seen_s;
    }
    records_.push_back(next);
    dirty_ = true;
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::find(
    const MeshPeerIdentity& identity,
    MeshPeerRecord& out_record)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = records_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::findByNodeId(
    MeshProtocol protocol,
    NodeId node_id,
    MeshPeerRecord& out_record)
{
    if (node_id == 0)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    for (const auto& record : records_)
    {
        if (!meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            continue;
        }

        if (record.identity.kind == MeshPeerIdentityKind::NodeId &&
            record.identity.node_id == node_id)
        {
            out_record = record;
            return MeshPeerDirectoryStatus::success();
        }

        if (meshPeerIsReticulumProtocol(protocol) &&
            record.identity.kind == MeshPeerIdentityKind::ReticulumDestination &&
            record.identity.reticulum.valid &&
            reticulumNodeIdFromDestinationHash(
                record.identity.reticulum.destination_hash) == node_id)
        {
            out_record = record;
            return MeshPeerDirectoryStatus::success();
        }
    }

    return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::NotFound);
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::loadRecent(
    MeshProtocol protocol,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!out_count || (!out_records && max_records > 0))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    std::vector<const MeshPeerRecord*> matches;
    for (const auto& record : records_)
    {
        if (meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            matches.push_back(&record);
        }
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });

    const std::size_t count_to_copy = std::min(max_records, matches.size());
    for (std::size_t index = 0; index < count_to_copy; ++index)
    {
        out_records[index] = *matches[index];
    }
    *out_count = count_to_copy;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::search(
    MeshProtocol protocol,
    const char* query,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    if (!query || !out_count || (!out_records && max_records > 0))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    *out_count = 0;
    for (const auto& record : records_)
    {
        if (!meshPeerSameProtocol(record.identity.protocol, protocol) ||
            !textContains(record.display_name, query))
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
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::setUserFlags(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    records_[index].flags = flags;
    dirty_ = true;
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::remove(
    const MeshPeerIdentity& identity)
{
    const std::size_t index = findIndex(identity);
    if (index >= records_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    records_.erase(records_.begin() + static_cast<std::ptrdiff_t>(index));
    dirty_ = true;
    maybeSave();
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::clearProtocol(MeshProtocol protocol)
{
    const auto old_size = records_.size();
    records_.erase(std::remove_if(records_.begin(),
                                  records_.end(),
                                  [protocol](const MeshPeerRecord& record)
                                  {
                                      return meshPeerSameProtocol(
                                          record.identity.protocol,
                                          protocol);
                                  }),
                   records_.end());
    if (records_.size() != old_size)
    {
        dirty_ = true;
        maybeSave();
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryCapacity MeshPeerDirectoryCore::capacityFor(
    MeshProtocol protocol) const
{
    if (protocol == MeshProtocol::Meshtastic)
    {
        return options_.meshtastic_capacity;
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return options_.meshcore_capacity;
    }
    if (meshPeerIsReticulumProtocol(protocol))
    {
        return options_.reticulum_capacity;
    }
    return {};
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::flush()
{
    if (!dirty_)
    {
        return MeshPeerDirectoryStatus::success();
    }
    return saveRecords();
}

std::size_t MeshPeerDirectoryCore::count(MeshProtocol protocol) const
{
    return countForProtocol(protocol);
}

void MeshPeerDirectoryCore::clear()
{
    records_.clear();
    dirty_ = true;
    blob_store_.clearBlob();
    dirty_ = false;
}

uint32_t MeshPeerDirectoryCore::computeBlobCrc(const uint8_t* data,
                                               std::size_t len)
{
    uint32_t hash = 2166136261UL;
    for (std::size_t index = 0; index < len; ++index)
    {
        hash ^= data ? data[index] : 0U;
        hash *= 16777619UL;
    }
    return hash;
}

bool MeshPeerDirectoryCore::decodeBlob(std::vector<MeshPeerRecord>& out,
                                       const uint8_t* data,
                                       std::size_t len)
{
    out.clear();
    if (!data || len < sizeof(PersistedMeshPeerDirectoryHeaderV1))
    {
        return false;
    }

    PersistedMeshPeerDirectoryHeaderV1 header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kMeshPeerDirectoryMagic ||
        header.version != kMeshPeerDirectoryPersistVersion)
    {
        return false;
    }

    const std::size_t entries_len = len - sizeof(header);
    if ((entries_len % sizeof(PersistedMeshPeerEntryV1)) != 0 ||
        (entries_len / sizeof(PersistedMeshPeerEntryV1)) != header.count)
    {
        return false;
    }

    const uint8_t* entries_data = data + sizeof(header);
    if (computeBlobCrc(entries_data, entries_len) != header.crc)
    {
        return false;
    }

    out.reserve(header.count);
    for (std::size_t index = 0; index < header.count; ++index)
    {
        PersistedMeshPeerEntryV1 persisted{};
        std::memcpy(&persisted,
                    entries_data + index * sizeof(PersistedMeshPeerEntryV1),
                    sizeof(persisted));
        MeshPeerRecord record{};
        if (copyFromPersisted(record, persisted))
        {
            out.push_back(record);
        }
    }
    return true;
}

void MeshPeerDirectoryCore::encodeBlob(
    std::vector<uint8_t>& out,
    const std::vector<MeshPeerRecord>& records)
{
    const std::size_t entries_len =
        records.size() * sizeof(PersistedMeshPeerEntryV1);
    out.assign(sizeof(PersistedMeshPeerDirectoryHeaderV1) + entries_len, 0);

    auto* entries_data = out.data() + sizeof(PersistedMeshPeerDirectoryHeaderV1);
    for (std::size_t index = 0; index < records.size(); ++index)
    {
        PersistedMeshPeerEntryV1 persisted{};
        copyIntoPersisted(persisted, records[index]);
        std::memcpy(entries_data + index * sizeof(PersistedMeshPeerEntryV1),
                    &persisted,
                    sizeof(persisted));
    }

    PersistedMeshPeerDirectoryHeaderV1 header{};
    header.count = static_cast<uint32_t>(records.size());
    header.crc = computeBlobCrc(entries_data, entries_len);
    std::memcpy(out.data(), &header, sizeof(header));
}

std::size_t MeshPeerDirectoryCore::findIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        if (sameMeshPeerIdentity(records_[index].identity, identity))
        {
            return index;
        }
    }
    return records_.size();
}

std::size_t MeshPeerDirectoryCore::countForProtocol(MeshProtocol protocol) const
{
    std::size_t result = 0;
    for (const auto& record : records_)
    {
        if (meshPeerSameProtocol(record.identity.protocol, protocol))
        {
            ++result;
        }
    }
    return result;
}

void MeshPeerDirectoryCore::evictOldest(MeshProtocol protocol)
{
    auto oldest = records_.end();
    for (auto it = records_.begin(); it != records_.end(); ++it)
    {
        if (!meshPeerSameProtocol(it->identity.protocol, protocol))
        {
            continue;
        }
        if (oldest == records_.end() || it->last_seen_s < oldest->last_seen_s)
        {
            oldest = it;
        }
    }
    if (oldest != records_.end())
    {
        records_.erase(oldest);
    }
}

MeshPeerDirectoryStatus MeshPeerDirectoryCore::saveRecords()
{
    std::vector<uint8_t> blob;
    encodeBlob(blob, records_);
    if (!blob_store_.saveBlob(blob.empty() ? nullptr : blob.data(), blob.size()))
    {
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    dirty_ = false;
    return MeshPeerDirectoryStatus::success();
}

void MeshPeerDirectoryCore::maybeSave()
{
    if (options_.auto_save)
    {
        (void)saveRecords();
    }
}

} // namespace chat
