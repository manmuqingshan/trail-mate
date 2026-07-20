#include "platform/esp/arduino_common/chat/infra/store/protocol_peer_codec.h"

#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"

#include <algorithm>
#include <cstring>

namespace chat::storage::v2
{
namespace
{

constexpr uint32_t kPeerMagic = 0x32524550U;    // PER2
constexpr uint32_t kContactMagic = 0x32544E43U; // CNT2

constexpr uint16_t kDeleted = 0x0001U;
constexpr uint16_t kFavorite = 0x0002U;
constexpr uint16_t kIgnored = 0x0004U;
constexpr uint16_t kTrusted = 0x0008U;
constexpr uint16_t kHasSnr = 0x0010U;
constexpr uint16_t kHasRssi = 0x0020U;
constexpr uint16_t kHasMetrics = 0x0040U;
constexpr uint16_t kHasPosition = 0x0080U;

constexpr uint8_t kHasBattery = 0x01U;
constexpr uint8_t kHasVoltage = 0x02U;
constexpr uint8_t kHasChannelUtilization = 0x04U;
constexpr uint8_t kHasAirUtilization = 0x08U;
constexpr uint8_t kHasUptime = 0x10U;
constexpr uint8_t kPositionValid = 0x01U;
constexpr uint8_t kPositionHasAltitude = 0x02U;

struct PeerPrefix
{
    uint32_t magic = kPeerMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint16_t flags = 0;
    uint8_t source = 0;
    uint8_t identity_kind = 0;
    uint16_t reserved = 0;
    uint32_t node_id = 0;
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    char display_name[kMeshPeerDisplayNameMaxLen] = {};
    float snr = 0.0f;
    float rssi = 0.0f;
    uint8_t metrics_flags = 0;
    uint8_t position_flags = 0;
    uint16_t reserved2 = 0;
    uint32_t battery_level = 0;
    float voltage = 0.0f;
    float channel_utilization = 0.0f;
    float air_util_tx = 0.0f;
    uint32_t uptime_seconds = 0;
    int32_t latitude_i = 0;
    int32_t longitude_i = 0;
    int32_t altitude = 0;
    uint32_t position_timestamp = 0;
    uint32_t precision_bits = 0;
    uint32_t pdop = 0;
    uint32_t hdop = 0;
    uint32_t vdop = 0;
    uint32_t gps_accuracy_mm = 0;
} __attribute__((packed));

struct NodeFactsSlot
{
    char short_name[kMeshPeerShortNameMaxLen] = {};
    char long_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t role = 0xFF;
    uint8_t hw_model = 0;
    uint8_t channel = 0xFF;
    uint8_t hops_away = 0xFF;
    uint8_t flags = 0;
    uint8_t macaddr[kMeshPeerMacAddrLen] = {};
} __attribute__((packed));

struct MeshtasticPeerSlot
{
    PeerPrefix prefix{};
    NodeFactsSlot node{};
    uint8_t has_public_key = 0;
    uint8_t key_verified = 0;
    uint8_t has_next_hop = 0;
    uint8_t next_hop = 0;
    uint8_t public_key[kMeshPeerMeshtasticPublicKeyLen] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct MeshCorePeerSlot
{
    PeerPrefix prefix{};
    NodeFactsSlot node{};
    uint8_t has_public_key = 0;
    uint8_t key_verified = 0;
    uint8_t has_peer_hash = 0;
    uint8_t peer_hash = 0;
    uint8_t has_next_hop = 0;
    uint8_t next_hop = 0;
    uint16_t reserved = 0;
    uint32_t node_id_hint = 0;
    uint8_t public_key[kMeshPeerMeshCorePublicKeyLen] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReticulumPeerSlot
{
    PeerPrefix prefix{};
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    uint8_t identity_hash[kReticulumPeerHashSize] = {};
    uint8_t has_public_keys = 0;
    uint8_t has_ratchet = 0;
    uint8_t delivery = 0;
    uint8_t propagation = 0;
    uint8_t enc_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t sig_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t ratchet_pub[kMeshPeerReticulumRatchetLen] = {};
    uint32_t ratchet_seen_s = 0;
    uint32_t crc = 0;
} __attribute__((packed));

struct NodeContactSlot
{
    uint32_t magic = kContactMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint16_t flags = 0;
    uint32_t node_id = 0;
    char alias[kMeshPeerUserAliasMaxLen + 1] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct MeshCoreContactSlot
{
    uint32_t magic = kContactMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint16_t flags = 0;
    uint32_t node_id_hint = 0;
    uint8_t public_key[kMeshPeerMeshCorePublicKeyLen] = {};
    char alias[kMeshPeerUserAliasMaxLen + 1] = {};
    uint32_t crc = 0;
} __attribute__((packed));

struct ReticulumContactSlot
{
    uint32_t magic = kContactMagic;
    uint16_t schema = kStorageSchemaVersion;
    uint16_t flags = 0;
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    char alias[kMeshPeerUserAliasMaxLen + 1] = {};
    uint32_t crc = 0;
} __attribute__((packed));

template <typename T>
bool validSlot(const T& slot, uint32_t magic)
{
    return slot.prefix.magic == magic &&
           slot.prefix.schema == kStorageSchemaVersion &&
           slot.crc == crc32(&slot, sizeof(slot) - sizeof(slot.crc));
}

template <typename T>
bool validContactSlot(const T& slot)
{
    return slot.magic == kContactMagic &&
           slot.schema == kStorageSchemaVersion &&
           slot.crc == crc32(&slot, sizeof(slot) - sizeof(slot.crc));
}

uint16_t userFlags(const MeshPeerUserFlags& flags, bool deleted)
{
    uint16_t result = deleted ? kDeleted : 0U;
    result |= flags.favorite ? kFavorite : 0U;
    result |= flags.ignored ? kIgnored : 0U;
    result |= flags.trusted ? kTrusted : 0U;
    return result;
}

MeshPeerUserFlags decodeUserFlags(uint16_t flags)
{
    MeshPeerUserFlags result{};
    result.favorite = (flags & kFavorite) != 0U;
    result.ignored = (flags & kIgnored) != 0U;
    result.trusted = (flags & kTrusted) != 0U;
    return result;
}

void encodeNodeFacts(NodeFactsSlot& out, const MeshPeerNodeFacts& facts)
{
    std::memcpy(out.short_name, facts.short_name, sizeof(out.short_name));
    std::memcpy(out.long_name, facts.long_name, sizeof(out.long_name));
    out.role = facts.role;
    out.hw_model = facts.hw_model;
    out.channel = facts.channel;
    out.hops_away = facts.hops_away;
    out.flags = facts.has_macaddr ? 0x01U : 0U;
    out.flags |= facts.via_mqtt ? 0x02U : 0U;
    std::memcpy(out.macaddr, facts.macaddr, sizeof(out.macaddr));
}

void decodeNodeFacts(MeshPeerNodeFacts& out, const NodeFactsSlot& facts)
{
    std::memcpy(out.short_name, facts.short_name, sizeof(out.short_name));
    out.short_name[sizeof(out.short_name) - 1U] = '\0';
    std::memcpy(out.long_name, facts.long_name, sizeof(out.long_name));
    out.long_name[sizeof(out.long_name) - 1U] = '\0';
    out.role = facts.role;
    out.hw_model = facts.hw_model;
    out.channel = facts.channel;
    out.hops_away = facts.hops_away;
    out.has_macaddr = (facts.flags & 0x01U) != 0U;
    out.via_mqtt = (facts.flags & 0x02U) != 0U;
    std::memcpy(out.macaddr, facts.macaddr, sizeof(out.macaddr));
}

void encodePrefix(PeerPrefix& out, const PeerProjection& projection)
{
    const MeshPeerRecord& record = projection.record;
    out.flags = projection.deleted ? kDeleted : 0U;
    out.source = static_cast<uint8_t>(record.source);
    out.identity_kind = static_cast<uint8_t>(record.identity.kind);
    out.node_id = record.identity.node_id;
    out.first_seen_s = record.first_seen_s;
    out.last_seen_s = record.last_seen_s;
    std::memcpy(out.display_name,
                record.display_name,
                sizeof(out.display_name));
    const MeshPeerObservations& observations = record.observations;
    if (observations.has_snr)
    {
        out.flags |= kHasSnr;
        out.snr = observations.snr;
    }
    if (observations.has_rssi)
    {
        out.flags |= kHasRssi;
        out.rssi = observations.rssi;
    }
    if (observations.has_device_metrics)
    {
        out.flags |= kHasMetrics;
        const contacts::NodeDeviceMetrics& metrics =
            observations.device_metrics;
        out.metrics_flags |= metrics.has_battery_level ? kHasBattery : 0U;
        out.metrics_flags |= metrics.has_voltage ? kHasVoltage : 0U;
        out.metrics_flags |= metrics.has_channel_utilization
                                 ? kHasChannelUtilization
                                 : 0U;
        out.metrics_flags |= metrics.has_air_util_tx ? kHasAirUtilization : 0U;
        out.metrics_flags |= metrics.has_uptime_seconds ? kHasUptime : 0U;
        out.battery_level = metrics.battery_level;
        out.voltage = metrics.voltage;
        out.channel_utilization = metrics.channel_utilization;
        out.air_util_tx = metrics.air_util_tx;
        out.uptime_seconds = metrics.uptime_seconds;
    }
    if (observations.has_position)
    {
        out.flags |= kHasPosition;
        const contacts::NodePosition& position = observations.position;
        out.position_flags = position.valid ? kPositionValid : 0U;
        out.position_flags |= position.has_altitude ? kPositionHasAltitude : 0U;
        out.latitude_i = position.latitude_i;
        out.longitude_i = position.longitude_i;
        out.altitude = position.altitude;
        out.position_timestamp = position.timestamp;
        out.precision_bits = position.precision_bits;
        out.pdop = position.pdop;
        out.hdop = position.hdop;
        out.vdop = position.vdop;
        out.gps_accuracy_mm = position.gps_accuracy_mm;
    }
}

void decodePrefix(MeshProtocol protocol,
                  const PeerPrefix& prefix,
                  PeerProjection& projection)
{
    projection = PeerProjection{};
    projection.deleted = (prefix.flags & kDeleted) != 0U;
    MeshPeerRecord& record = projection.record;
    record.valid = true;
    record.identity.protocol = canonicalProtocol(protocol);
    record.identity.kind =
        static_cast<MeshPeerIdentityKind>(prefix.identity_kind);
    record.identity.node_id = prefix.node_id;
    record.source = static_cast<MeshPeerSource>(prefix.source);
    record.first_seen_s = prefix.first_seen_s;
    record.last_seen_s = prefix.last_seen_s;
    std::memcpy(record.display_name,
                prefix.display_name,
                sizeof(record.display_name));
    record.display_name[sizeof(record.display_name) - 1U] = '\0';
    record.observations.has_snr = (prefix.flags & kHasSnr) != 0U;
    record.observations.snr = prefix.snr;
    record.observations.has_rssi = (prefix.flags & kHasRssi) != 0U;
    record.observations.rssi = prefix.rssi;
    record.observations.has_device_metrics =
        (prefix.flags & kHasMetrics) != 0U;
    contacts::NodeDeviceMetrics& metrics =
        record.observations.device_metrics;
    metrics.has_battery_level = (prefix.metrics_flags & kHasBattery) != 0U;
    metrics.battery_level = prefix.battery_level;
    metrics.has_voltage = (prefix.metrics_flags & kHasVoltage) != 0U;
    metrics.voltage = prefix.voltage;
    metrics.has_channel_utilization =
        (prefix.metrics_flags & kHasChannelUtilization) != 0U;
    metrics.channel_utilization = prefix.channel_utilization;
    metrics.has_air_util_tx =
        (prefix.metrics_flags & kHasAirUtilization) != 0U;
    metrics.air_util_tx = prefix.air_util_tx;
    metrics.has_uptime_seconds = (prefix.metrics_flags & kHasUptime) != 0U;
    metrics.uptime_seconds = prefix.uptime_seconds;
    record.observations.has_position =
        (prefix.flags & kHasPosition) != 0U;
    contacts::NodePosition& position = record.observations.position;
    position.valid = (prefix.position_flags & kPositionValid) != 0U;
    position.has_altitude =
        (prefix.position_flags & kPositionHasAltitude) != 0U;
    position.latitude_i = prefix.latitude_i;
    position.longitude_i = prefix.longitude_i;
    position.altitude = prefix.altitude;
    position.timestamp = prefix.position_timestamp;
    position.precision_bits = prefix.precision_bits;
    position.pdop = prefix.pdop;
    position.hdop = prefix.hdop;
    position.vdop = prefix.vdop;
    position.gps_accuracy_mm = prefix.gps_accuracy_mm;
}

bool validPeerIdentityForProtocol(MeshProtocol protocol,
                                  const MeshPeerIdentity& identity)
{
    protocol = canonicalProtocol(protocol);
    return meshPeerSameProtocol(protocol, identity.protocol) &&
           ((protocol == MeshProtocol::Meshtastic &&
             identity.kind == MeshPeerIdentityKind::NodeId) ||
            (protocol == MeshProtocol::MeshCore &&
             ((identity.kind == MeshPeerIdentityKind::NodeId &&
               identity.node_id != 0U) ||
              (identity.kind == MeshPeerIdentityKind::PublicKey &&
               identity.public_key_len == kMeshPeerMeshCorePublicKeyLen))) ||
            (protocol == MeshProtocol::Reticulum &&
             ((identity.kind == MeshPeerIdentityKind::NodeId &&
               identity.node_id != 0U) ||
              identity.kind == MeshPeerIdentityKind::ReticulumDestination)));
}

bool validContactIdentityForProtocol(MeshProtocol protocol,
                                     const MeshPeerIdentity& identity)
{
    protocol = canonicalProtocol(protocol);
    return meshPeerSameProtocol(protocol, identity.protocol) &&
           ((protocol == MeshProtocol::Meshtastic &&
             identity.kind == MeshPeerIdentityKind::NodeId) ||
            (protocol == MeshProtocol::MeshCore &&
             identity.kind == MeshPeerIdentityKind::PublicKey &&
             identity.public_key_len == kMeshPeerMeshCorePublicKeyLen) ||
            (protocol == MeshProtocol::Reticulum &&
             identity.kind == MeshPeerIdentityKind::ReticulumDestination));
}

} // namespace

std::size_t peerSlotSize(MeshProtocol protocol)
{
    switch (canonicalProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
        return sizeof(MeshtasticPeerSlot);
    case MeshProtocol::MeshCore:
        return sizeof(MeshCorePeerSlot);
    case MeshProtocol::Reticulum:
        return sizeof(ReticulumPeerSlot);
    default:
        return 0U;
    }
}

std::size_t contactSlotSize(MeshProtocol protocol)
{
    switch (canonicalProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
        return sizeof(NodeContactSlot);
    case MeshProtocol::MeshCore:
        return sizeof(MeshCoreContactSlot);
    case MeshProtocol::Reticulum:
        return sizeof(ReticulumContactSlot);
    default:
        return 0U;
    }
}

bool encodePeerSlot(MeshProtocol protocol,
                    const PeerProjection& projection,
                    void* out,
                    std::size_t out_len)
{
    protocol = canonicalProtocol(protocol);
    if (!out || !meshPeerRecordIsValid(projection.record) ||
        !validPeerIdentityForProtocol(protocol, projection.record.identity))
    {
        return false;
    }
    if (protocol == MeshProtocol::Meshtastic &&
        out_len == sizeof(MeshtasticPeerSlot))
    {
        MeshtasticPeerSlot slot{};
        encodePrefix(slot.prefix, projection);
        encodeNodeFacts(slot.node, projection.record.meshtastic.node);
        slot.has_public_key = projection.record.meshtastic.has_public_key ? 1U : 0U;
        slot.key_verified =
            projection.record.meshtastic.key_manually_verified ? 1U : 0U;
        slot.has_next_hop =
            projection.record.meshtastic.has_next_hop ? 1U : 0U;
        slot.next_hop = projection.record.meshtastic.next_hop;
        std::memcpy(slot.public_key,
                    projection.record.meshtastic.public_key,
                    sizeof(slot.public_key));
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (protocol == MeshProtocol::MeshCore &&
        out_len == sizeof(MeshCorePeerSlot))
    {
        MeshCorePeerSlot slot{};
        encodePrefix(slot.prefix, projection);
        encodeNodeFacts(slot.node, projection.record.meshcore.node);
        slot.has_public_key = projection.record.meshcore.has_public_key ? 1U : 0U;
        slot.key_verified =
            projection.record.meshcore.public_key_verified ? 1U : 0U;
        slot.has_peer_hash = projection.record.meshcore.has_peer_hash ? 1U : 0U;
        slot.peer_hash = projection.record.meshcore.peer_hash;
        slot.has_next_hop = projection.record.meshcore.has_next_hop ? 1U : 0U;
        slot.next_hop = projection.record.meshcore.next_hop;
        slot.node_id_hint = projection.record.meshcore.node_id_hint;
        if (projection.record.identity.kind ==
            MeshPeerIdentityKind::PublicKey)
        {
            std::memcpy(slot.public_key,
                        projection.record.identity.public_key,
                        sizeof(slot.public_key));
        }
        else if (projection.record.meshcore.has_public_key)
        {
            std::memcpy(slot.public_key,
                        projection.record.meshcore.public_key,
                        sizeof(slot.public_key));
        }
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (protocol == MeshProtocol::Reticulum &&
        out_len == sizeof(ReticulumPeerSlot))
    {
        ReticulumPeerSlot slot{};
        encodePrefix(slot.prefix, projection);
        const ReticulumPeerFacts& facts = projection.record.reticulum;
        if (projection.record.identity.kind ==
            MeshPeerIdentityKind::ReticulumDestination)
        {
            (void)copyReticulumIdentityHashes(
                slot.destination_hash,
                slot.identity_hash,
                projection.record.identity.reticulum);
        }
        slot.has_public_keys = facts.has_public_keys ? 1U : 0U;
        slot.has_ratchet = facts.has_ratchet ? 1U : 0U;
        slot.delivery = facts.delivery ? 1U : 0U;
        slot.propagation = facts.propagation ? 1U : 0U;
        std::memcpy(slot.enc_pub, facts.enc_pub, sizeof(slot.enc_pub));
        std::memcpy(slot.sig_pub, facts.sig_pub, sizeof(slot.sig_pub));
        std::memcpy(slot.ratchet_pub,
                    facts.ratchet_pub,
                    sizeof(slot.ratchet_pub));
        slot.ratchet_seen_s = facts.ratchet_seen_s;
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    return false;
}

bool decodePeerSlot(MeshProtocol protocol,
                    const void* data,
                    std::size_t len,
                    PeerProjection& out_projection)
{
    protocol = canonicalProtocol(protocol);
    if (!data)
    {
        return false;
    }
    if (protocol == MeshProtocol::Meshtastic &&
        len == sizeof(MeshtasticPeerSlot))
    {
        MeshtasticPeerSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validSlot(slot, kPeerMagic))
        {
            return false;
        }
        decodePrefix(protocol, slot.prefix, out_projection);
        out_projection.record.identity = makeMeshPeerNodeIdentity(
            protocol,
            slot.prefix.node_id);
        decodeNodeFacts(out_projection.record.meshtastic.node, slot.node);
        out_projection.record.meshtastic.has_public_key = slot.has_public_key != 0U;
        out_projection.record.meshtastic.key_manually_verified =
            slot.key_verified != 0U;
        out_projection.record.meshtastic.has_next_hop =
            slot.has_next_hop != 0U;
        out_projection.record.meshtastic.next_hop = slot.next_hop;
        std::memcpy(out_projection.record.meshtastic.public_key,
                    slot.public_key,
                    sizeof(slot.public_key));
        return meshPeerRecordIsValid(out_projection.record);
    }
    if (protocol == MeshProtocol::MeshCore &&
        len == sizeof(MeshCorePeerSlot))
    {
        MeshCorePeerSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validSlot(slot, kPeerMagic))
        {
            return false;
        }
        MeshPeerIdentity identity{};
        if (static_cast<MeshPeerIdentityKind>(slot.prefix.identity_kind) ==
            MeshPeerIdentityKind::NodeId)
        {
            identity = makeMeshPeerNodeIdentity(protocol,
                                                slot.prefix.node_id);
        }
        else if (!makeMeshPeerPublicKeyIdentity(protocol,
                                                slot.public_key,
                                                sizeof(slot.public_key),
                                                identity))
        {
            return false;
        }
        decodePrefix(protocol, slot.prefix, out_projection);
        out_projection.record.identity = identity;
        decodeNodeFacts(out_projection.record.meshcore.node, slot.node);
        out_projection.record.meshcore.has_public_key = slot.has_public_key != 0U;
        out_projection.record.meshcore.public_key_verified =
            slot.key_verified != 0U;
        out_projection.record.meshcore.has_peer_hash = slot.has_peer_hash != 0U;
        out_projection.record.meshcore.peer_hash = slot.peer_hash;
        out_projection.record.meshcore.has_next_hop = slot.has_next_hop != 0U;
        out_projection.record.meshcore.next_hop = slot.next_hop;
        out_projection.record.meshcore.node_id_hint = slot.node_id_hint;
        std::memcpy(out_projection.record.meshcore.public_key,
                    slot.public_key,
                    sizeof(slot.public_key));
        return meshPeerRecordIsValid(out_projection.record);
    }
    if (protocol == MeshProtocol::Reticulum &&
        len == sizeof(ReticulumPeerSlot))
    {
        ReticulumPeerSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validSlot(slot, kPeerMagic))
        {
            return false;
        }
        decodePrefix(protocol, slot.prefix, out_projection);
        if (static_cast<MeshPeerIdentityKind>(slot.prefix.identity_kind) ==
            MeshPeerIdentityKind::NodeId)
        {
            out_projection.record.identity = makeMeshPeerNodeIdentity(
                protocol,
                slot.prefix.node_id);
        }
        else
        {
            out_projection.record.identity = makeMeshPeerReticulumIdentity(
                makeReticulumPeerIdentity(slot.destination_hash,
                                          slot.identity_hash));
        }
        ReticulumPeerFacts& facts = out_projection.record.reticulum;
        if (out_projection.record.identity.kind ==
            MeshPeerIdentityKind::ReticulumDestination)
        {
            facts.identity = out_projection.record.identity.reticulum;
        }
        facts.has_public_keys = slot.has_public_keys != 0U;
        facts.has_ratchet = slot.has_ratchet != 0U;
        facts.delivery = slot.delivery != 0U;
        facts.propagation = slot.propagation != 0U;
        std::memcpy(facts.enc_pub, slot.enc_pub, sizeof(facts.enc_pub));
        std::memcpy(facts.sig_pub, slot.sig_pub, sizeof(facts.sig_pub));
        std::memcpy(facts.ratchet_pub,
                    slot.ratchet_pub,
                    sizeof(facts.ratchet_pub));
        facts.ratchet_seen_s = slot.ratchet_seen_s;
        return meshPeerRecordIsValid(out_projection.record);
    }
    return false;
}

bool encodeContactSlot(MeshProtocol protocol,
                       const ContactProjection& projection,
                       void* out,
                       std::size_t out_len)
{
    protocol = canonicalProtocol(protocol);
    if (!out ||
        !validContactIdentityForProtocol(protocol, projection.identity))
    {
        return false;
    }
    const uint16_t flags = userFlags(projection.flags, projection.deleted);
    if (protocol == MeshProtocol::Meshtastic &&
        out_len == sizeof(NodeContactSlot))
    {
        NodeContactSlot slot{};
        slot.flags = flags;
        slot.node_id = projection.identity.node_id;
        std::memcpy(slot.alias, projection.alias, sizeof(slot.alias));
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (protocol == MeshProtocol::MeshCore &&
        out_len == sizeof(MeshCoreContactSlot))
    {
        MeshCoreContactSlot slot{};
        slot.flags = flags;
        slot.node_id_hint = projection.node_id_hint;
        std::memcpy(slot.public_key,
                    projection.identity.public_key,
                    sizeof(slot.public_key));
        std::memcpy(slot.alias, projection.alias, sizeof(slot.alias));
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    if (protocol == MeshProtocol::Reticulum &&
        out_len == sizeof(ReticulumContactSlot))
    {
        ReticulumContactSlot slot{};
        slot.flags = flags;
        std::memcpy(slot.destination_hash,
                    projection.identity.reticulum.destination_hash,
                    sizeof(slot.destination_hash));
        std::memcpy(slot.alias, projection.alias, sizeof(slot.alias));
        slot.crc = crc32(&slot, sizeof(slot) - sizeof(slot.crc));
        std::memcpy(out, &slot, sizeof(slot));
        return true;
    }
    return false;
}

bool decodeContactSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ContactProjection& out_projection)
{
    protocol = canonicalProtocol(protocol);
    out_projection = ContactProjection{};
    if (!data)
    {
        return false;
    }
    uint16_t flags = 0;
    if (protocol == MeshProtocol::Meshtastic &&
        len == sizeof(NodeContactSlot))
    {
        NodeContactSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validContactSlot(slot))
        {
            return false;
        }
        flags = slot.flags;
        out_projection.identity = makeMeshPeerNodeIdentity(protocol,
                                                           slot.node_id);
        std::memcpy(out_projection.alias, slot.alias, sizeof(slot.alias));
    }
    else if (protocol == MeshProtocol::MeshCore &&
             len == sizeof(MeshCoreContactSlot))
    {
        MeshCoreContactSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validContactSlot(slot) ||
            !makeMeshPeerPublicKeyIdentity(protocol,
                                           slot.public_key,
                                           sizeof(slot.public_key),
                                           out_projection.identity))
        {
            return false;
        }
        flags = slot.flags;
        out_projection.node_id_hint = slot.node_id_hint;
        std::memcpy(out_projection.alias, slot.alias, sizeof(slot.alias));
    }
    else if (protocol == MeshProtocol::Reticulum &&
             len == sizeof(ReticulumContactSlot))
    {
        ReticulumContactSlot slot{};
        std::memcpy(&slot, data, sizeof(slot));
        if (!validContactSlot(slot))
        {
            return false;
        }
        flags = slot.flags;
        out_projection.identity = makeMeshPeerReticulumIdentity(
            makeReticulumDestinationIdentity(slot.destination_hash));
        std::memcpy(out_projection.alias, slot.alias, sizeof(slot.alias));
    }
    else
    {
        return false;
    }
    out_projection.alias[sizeof(out_projection.alias) - 1U] = '\0';
    out_projection.flags = decodeUserFlags(flags);
    out_projection.deleted = (flags & kDeleted) != 0U;
    return meshPeerIdentityIsValid(out_projection.identity);
}

} // namespace chat::storage::v2
