/**
 * @file meshcore_payload_helpers.cpp
 * @brief Shared MeshCore payload/discovery helper utilities
 */

#include "chat/infra/meshcore/meshcore_payload_helpers.h"

#include "chat/domain/contact_types.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

#include <cstdio>
#include <cstring>

namespace chat
{
namespace meshcore
{
namespace
{
constexpr uint8_t kRouteTypeTransportFlood = 0x00;
constexpr uint8_t kPayloadTypeReq = 0x00;
constexpr uint8_t kPayloadTypeResponse = 0x01;
constexpr uint8_t kPayloadTypeTxtMsg = 0x02;
constexpr uint8_t kPayloadTypeGrpTxt = 0x05;
constexpr uint8_t kPayloadTypeAnonReq = 0x07;
constexpr uint8_t kPayloadTypeDirectData = kPayloadTypeAnonReq;
constexpr uint8_t kPayloadTypePath = 0x08;
constexpr size_t kMeshcoreMaxPathSize = 64;
constexpr size_t kMeshcoreMaxFrameSize = 255;
constexpr size_t kCipherBlockSize = 16;
constexpr size_t kMeshcorePubKeySize = 32;
constexpr size_t kMeshcorePubKeyPrefixSize = 8;
constexpr NodeId kSyntheticNodePrefix = 0x4D430000UL;
constexpr uint8_t kDirectAppMagic0 = 0xDA;
constexpr uint8_t kDirectAppMagic1 = 0x7A;
constexpr uint8_t kDirectAppFlagWantAck = 0x01;
constexpr uint8_t kGroupDataMagic0 = 0x47;
constexpr uint8_t kGroupDataMagic1 = 0x44;
constexpr uint8_t kControlMagic0 = 0x54;
constexpr uint8_t kControlMagic1 = 0x4D;
constexpr uint8_t kMeshCoreNodeInfoTypeQuery =
    static_cast<uint8_t>(MeshCoreNodeInfoControlType::Query);
constexpr uint8_t kMeshCoreNodeInfoTypeInfo =
    static_cast<uint8_t>(MeshCoreNodeInfoControlType::Info);
constexpr uint8_t kControlSubtypeDiscoverReq = 0x80;
constexpr uint8_t kControlSubtypeDiscoverResp = 0x90;
constexpr uint8_t kControlSubtypeMask = 0xF0;
constexpr uint8_t kDiscoverPrefixOnlyMask = 0x01;
constexpr uint8_t kAdvertTypeNone = 0x00;
constexpr uint8_t kAdvertTypeRoom = 0x03;
constexpr uint8_t kAdvertTypeSensor = 0x04;
constexpr uint8_t kAdvertFlagHasLocation = 0x10;
constexpr uint8_t kAdvertFlagHasFeature1 = 0x20;
constexpr uint8_t kAdvertFlagHasFeature2 = 0x40;
constexpr uint8_t kAdvertFlagHasName = 0x80;

constexpr uint8_t kPublicGroupPsk[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
    0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};

using chat::meshcore::buildHeader;
using chat::meshcore::encryptThenMac;
using chat::meshcore::isZeroKey;

void copyFixedNameBytes(char* out, size_t out_len, const char* src)
{
    if (!out || out_len == 0)
    {
        return;
    }
    memset(out, 0, out_len);
    if (!src)
    {
        return;
    }
    size_t index = 0;
    while (index + 1 < out_len && src[index] != '\0')
    {
        out[index] = src[index];
        ++index;
    }
}

} // namespace

bool shouldUsePublicChannelFallback(const chat::MeshConfig& cfg)
{
    const bool has_primary = !isZeroKey(cfg.primary_key, chat::kMeshCoreChannelKeyLen);
    const bool has_secondary = !isZeroKey(cfg.secondary_key, chat::kMeshCoreChannelKeyLen);
    if (!has_primary && !has_secondary)
    {
        return true;
    }

    if (cfg.meshcore_channel_name[0] == '\0')
    {
        return true;
    }

    static constexpr char kPublic[] = "public";
    for (size_t index = 0; index < sizeof(kPublic) - 1; ++index)
    {
        char c = cfg.meshcore_channel_name[index];
        if (c == '\0')
        {
            return false;
        }
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
        if (c != kPublic[index])
        {
            return false;
        }
    }
    return true;
}

const uint8_t* publicGroupPsk()
{
    return kPublicGroupPsk;
}

void xorCrypt(uint8_t* data, size_t len, const uint8_t* key, size_t key_len)
{
    if (!data || len == 0 || !key || key_len == 0)
    {
        return;
    }
    for (size_t index = 0; index < len; ++index)
    {
        data[index] ^= key[index % key_len];
    }
}

const uint8_t* selectChannelKey(const chat::MeshConfig& cfg, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!isZeroKey(cfg.secondary_key, chat::kMeshCoreChannelKeyLen))
    {
        if (out_len)
        {
            *out_len = chat::kMeshCoreChannelKeyLen;
        }
        return cfg.secondary_key;
    }
    if (!isZeroKey(cfg.primary_key, chat::kMeshCoreChannelKeyLen))
    {
        if (out_len)
        {
            *out_len = chat::kMeshCoreChannelKeyLen;
        }
        return cfg.primary_key;
    }
    return nullptr;
}

bool isPeerPayloadType(uint8_t payload_type)
{
    return payload_type == kPayloadTypeTxtMsg ||
           payload_type == kPayloadTypeDirectData ||
           payload_type == kPayloadTypeReq ||
           payload_type == kPayloadTypeResponse ||
           payload_type == kPayloadTypePath;
}

bool isPeerCipherShape(size_t payload_len)
{
    return isPeerCipherShape(PayloadProfile::V1, payload_len);
}

bool isPeerCipherShape(PayloadProfile profile, size_t payload_len)
{
    const size_t hash_bytes = payloadHashBytes(profile);
    const size_t mac_bytes = payloadMacBytes(profile);
    const size_t prefix_len = hash_bytes * 2U;
    if (payload_len <= (prefix_len + mac_bytes))
    {
        return false;
    }
    const size_t enc_len = payload_len - prefix_len - mac_bytes;
    return (enc_len % kCipherBlockSize) == 0;
}

bool isAnonReqCipherShape(size_t payload_len)
{
    return isAnonReqCipherShape(PayloadProfile::V1, payload_len);
}

bool isAnonReqCipherShape(PayloadProfile profile, size_t payload_len)
{
    const size_t hash_bytes = payloadHashBytes(profile);
    const size_t mac_bytes = payloadMacBytes(profile);
    const size_t prefix_len = hash_bytes + kMeshcorePubKeySize;
    if (payload_len <= (prefix_len + mac_bytes))
    {
        return false;
    }
    const size_t enc_len = payload_len - prefix_len - mac_bytes;
    return (enc_len % kCipherBlockSize) == 0;
}

bool buildFrameNoTransport(uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len)
{
    return buildFrameNoTransport(PayloadProfile::V1,
                                 route_type, payload_type,
                                 path, path_len,
                                 payload, payload_len,
                                 out_frame, out_cap, out_len);
}

bool buildFrameNoTransport(PayloadProfile profile,
                           uint8_t route_type, uint8_t payload_type,
                           const uint8_t* path, size_t path_len,
                           const uint8_t* payload, size_t payload_len,
                           uint8_t* out_frame, size_t out_cap, size_t* out_len)
{
    if (!out_frame || !out_len || !payload || payload_len == 0 ||
        out_cap == 0 || path_len > kMeshcoreMaxPathSize ||
        (path_len > 0 && !path) ||
        !pathIsWellFormed(profile, path_len))
    {
        return false;
    }
    if (route_type == kRouteTypeTransportFlood)
    {
        return false;
    }
    if ((2 + path_len + payload_len) > kMeshcoreMaxFrameSize)
    {
        return false;
    }

    size_t index = 0;
    out_frame[index++] = buildHeader(route_type, payload_type, payloadVersion(profile));
    out_frame[index++] = static_cast<uint8_t>(path_len);
    if (path_len > 0)
    {
        memcpy(&out_frame[index], path, path_len);
        index += path_len;
    }
    if (index + payload_len > out_cap)
    {
        return false;
    }
    memcpy(&out_frame[index], payload, payload_len);
    index += payload_len;
    *out_len = index;
    return true;
}

bool buildPeerDatagramPayload(uint8_t dest_hash, uint8_t src_hash,
                              const uint8_t key16[16], const uint8_t key32[32],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    return buildPeerDatagramPayload(PayloadProfile::V1,
                                    &dest_hash, &src_hash,
                                    key16, key32,
                                    plain, plain_len,
                                    out_payload, out_cap, out_len);
}

bool buildPeerDatagramPayload(PayloadProfile profile,
                              const uint8_t* dest_hash, const uint8_t* src_hash,
                              const uint8_t key16[16], const uint8_t key32[32],
                              const uint8_t* plain, size_t plain_len,
                              uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    const size_t hash_bytes = payloadHashBytes(profile);
    const size_t prefix_len = hash_bytes * 2U;
    if (!dest_hash || !src_hash || !key16 || !key32 || !plain || plain_len == 0 ||
        !out_payload || !out_len || out_cap <= prefix_len)
    {
        return false;
    }

    size_t index = 0;
    memcpy(&out_payload[index], dest_hash, hash_bytes);
    index += hash_bytes;
    memcpy(&out_payload[index], src_hash, hash_bytes);
    index += hash_bytes;
    const size_t encrypted_len = encryptThenMac(key16, key32,
                                                &out_payload[index], out_cap - index,
                                                plain, plain_len,
                                                payloadMacBytes(profile));
    if (encrypted_len == 0)
    {
        return false;
    }
    index += encrypted_len;
    *out_len = index;
    return true;
}

bool decodeDirectAppPayload(const uint8_t* plain, size_t plain_len, DecodedDirectAppPayload* out)
{
    if (!plain || !out || plain_len < sizeof(uint32_t))
    {
        return false;
    }

    *out = DecodedDirectAppPayload{};

    if (plain_len >= (2 + 1 + sizeof(uint32_t)) &&
        plain[0] == kDirectAppMagic0 &&
        plain[1] == kDirectAppMagic1)
    {
        const uint8_t flags = plain[2];
        memcpy(&out->portnum, plain + 3, sizeof(uint32_t));
        out->payload = plain + 3 + sizeof(uint32_t);
        out->payload_len = plain_len - (3 + sizeof(uint32_t));
        out->want_ack = (flags & kDirectAppFlagWantAck) != 0;
        return true;
    }

    memcpy(&out->portnum, plain, sizeof(uint32_t));
    out->payload = plain + sizeof(uint32_t);
    out->payload_len = plain_len - sizeof(uint32_t);
    return true;
}

bool decodeGroupAppPayload(const uint8_t* plain, size_t plain_len, DecodedGroupAppPayload* out)
{
    if (!plain || !out || plain_len < sizeof(uint32_t))
    {
        return false;
    }

    *out = DecodedGroupAppPayload{};

    if (plain_len >= (2 + sizeof(uint32_t) + sizeof(uint32_t)) &&
        plain[0] == kGroupDataMagic0 &&
        plain[1] == kGroupDataMagic1)
    {
        memcpy(&out->sender, plain + 2, sizeof(uint32_t));
        memcpy(&out->portnum, plain + 2 + sizeof(uint32_t), sizeof(uint32_t));
        out->payload = plain + 2 + sizeof(uint32_t) + sizeof(uint32_t);
        out->payload_len = plain_len - (2 + sizeof(uint32_t) + sizeof(uint32_t));
        return true;
    }

    memcpy(&out->portnum, plain, sizeof(uint32_t));
    out->payload = plain + sizeof(uint32_t);
    out->payload_len = plain_len - sizeof(uint32_t);
    return true;
}

bool hasControlPrefix(const uint8_t* payload, size_t len, uint8_t kind)
{
    return payload && len >= 4 &&
           payload[0] == kControlMagic0 &&
           payload[1] == kControlMagic1 &&
           payload[2] == kind;
}

bool buildNodeInfoQueryControlPayload(bool request_reply,
                                      uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    if (!out_payload || !out_len || out_cap < kMeshCoreNodeInfoQueryPayloadSize)
    {
        return false;
    }

    size_t index = 0;
    out_payload[index++] = kControlMagic0;
    out_payload[index++] = kControlMagic1;
    out_payload[index++] = kMeshCoreControlKindNodeInfo;
    out_payload[index++] = kMeshCoreNodeInfoTypeQuery;
    out_payload[index++] = request_reply ? kMeshCoreNodeInfoFlagRequestReply : 0x00;
    *out_len = index;
    return true;
}

bool buildNodeInfoInfoControlPayload(const MeshCoreNodeInfoBuildInfo& info,
                                     uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    if (!out_payload || !out_len || out_cap < kMeshCoreNodeInfoInfoPayloadSize)
    {
        return false;
    }

    char short_name[kMeshCoreNodeInfoShortNameFieldSize] = {};
    char long_name[kMeshCoreNodeInfoLongNameFieldSize] = {};
    copyFixedNameBytes(short_name, sizeof(short_name), info.short_name);
    copyFixedNameBytes(long_name, sizeof(long_name), info.long_name ? info.long_name : info.short_name);

    size_t index = 0;
    out_payload[index++] = kControlMagic0;
    out_payload[index++] = kControlMagic1;
    out_payload[index++] = kMeshCoreControlKindNodeInfo;
    out_payload[index++] = kMeshCoreNodeInfoTypeInfo;
    out_payload[index++] = info.role;
    out_payload[index++] = info.hops;
    memcpy(&out_payload[index], &info.node_id, sizeof(info.node_id));
    index += sizeof(info.node_id);
    memcpy(&out_payload[index], &info.timestamp, sizeof(info.timestamp));
    index += sizeof(info.timestamp);
    memcpy(&out_payload[index], short_name, sizeof(short_name));
    index += sizeof(short_name);
    memcpy(&out_payload[index], long_name, sizeof(long_name));
    index += sizeof(long_name);
    *out_len = index;
    return true;
}

bool decodeNodeInfoControlPayload(const uint8_t* payload, size_t len, DecodedNodeInfoControl* out)
{
    if (!out)
    {
        return false;
    }

    *out = DecodedNodeInfoControl{};
    if (!hasControlPrefix(payload, len, kMeshCoreControlKindNodeInfo))
    {
        return false;
    }

    out->valid = true;
    const uint8_t raw_type = payload[3];
    if (raw_type == kMeshCoreNodeInfoTypeQuery)
    {
        out->type = MeshCoreNodeInfoControlType::Query;
        out->complete = true;
        out->request_reply = (len > 4) &&
                             ((payload[4] & kMeshCoreNodeInfoFlagRequestReply) != 0);
        return true;
    }

    if (raw_type != kMeshCoreNodeInfoTypeInfo)
    {
        return true;
    }

    out->type = MeshCoreNodeInfoControlType::Info;
    if (len < kMeshCoreNodeInfoInfoPayloadSize)
    {
        return true;
    }

    size_t index = 4;
    out->info.role = payload[index++];
    out->info.hops = payload[index++];
    memcpy(&out->info.node_id, payload + index, sizeof(out->info.node_id));
    index += sizeof(out->info.node_id);
    memcpy(&out->info.timestamp, payload + index, sizeof(out->info.timestamp));
    index += sizeof(out->info.timestamp);
    memcpy(out->info.short_name, payload + index, kMeshCoreNodeInfoShortNameFieldSize);
    out->info.short_name[kMeshCoreNodeInfoShortNameFieldSize] = '\0';
    index += kMeshCoreNodeInfoShortNameFieldSize;
    memcpy(out->info.long_name, payload + index, kMeshCoreNodeInfoLongNameFieldSize);
    out->info.long_name[kMeshCoreNodeInfoLongNameFieldSize] = '\0';
    out->complete = true;
    return true;
}

size_t copySanitizedName(char* out, size_t out_len, const uint8_t* src, size_t src_len)
{
    if (!out || out_len == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!src || src_len == 0)
    {
        return 0;
    }

    size_t written = 0;
    for (size_t index = 0; index < src_len && written + 1 < out_len; ++index)
    {
        const uint8_t c = src[index];
        if (c == 0)
        {
            break;
        }
        if (c >= 0x20 && c <= 0x7E)
        {
            out[written++] = static_cast<char>(c);
        }
    }
    out[written] = '\0';
    return written;
}

size_t copyPrintableAscii(const std::string& src, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (src.empty())
    {
        return 0;
    }

    size_t written = 0;
    for (char c : src)
    {
        const uint8_t uc = static_cast<uint8_t>(c);
        if (uc >= 0x20 && uc <= 0x7E)
        {
            if (written + 1 >= out_len)
            {
                break;
            }
            out[written++] = static_cast<char>(uc);
        }
    }
    out[written] = '\0';
    return written;
}

uint8_t mapAdvertTypeToRole(uint8_t node_type)
{
    switch (node_type)
    {
    case kMeshCoreAdvertTypeChat:
        return static_cast<uint8_t>(chat::contacts::NodeRoleType::Client);
    case kMeshCoreAdvertTypeRepeater:
        return static_cast<uint8_t>(chat::contacts::NodeRoleType::Repeater);
    case kAdvertTypeRoom:
        return static_cast<uint8_t>(chat::contacts::NodeRoleType::Router);
    case kAdvertTypeSensor:
        return static_cast<uint8_t>(chat::contacts::NodeRoleType::Sensor);
    default:
        return static_cast<uint8_t>(chat::contacts::NodeRoleType::Unknown);
    }
}

bool discoverFilterMatchesType(uint8_t filter, uint8_t node_type)
{
    if (node_type == kAdvertTypeNone || node_type >= 8)
    {
        return false;
    }
    return (filter & static_cast<uint8_t>(1U << node_type)) != 0;
}

NodeId deriveNodeIdFromPubkey(const uint8_t* pubkey, size_t pubkey_len)
{
    if (!pubkey || pubkey_len == 0)
    {
        return 0;
    }

    NodeId node = 0;
    if (pubkey_len >= sizeof(node))
    {
        memcpy(&node, pubkey, sizeof(node));
        node = (node & 0xFFFFFF00UL) | static_cast<NodeId>(pubkey[0]);
    }

    if (node == 0)
    {
        node = kSyntheticNodePrefix | static_cast<NodeId>(pubkey[0]);
    }
    return node;
}

bool decodeAdvertAppData(const uint8_t* app_data, size_t app_data_len, DecodedAdvertAppData* out)
{
    if (!out)
    {
        return false;
    }

    *out = DecodedAdvertAppData{};
    if (!app_data || app_data_len == 0)
    {
        out->valid = true;
        return true;
    }

    size_t index = 0;
    const uint8_t flags = app_data[index++];
    out->node_type = flags & 0x0F;

    if ((flags & kAdvertFlagHasLocation) != 0)
    {
        if ((index + sizeof(int32_t) + sizeof(int32_t)) > app_data_len)
        {
            return false;
        }
        memcpy(&out->latitude_i6, app_data + index, sizeof(int32_t));
        index += sizeof(int32_t);
        memcpy(&out->longitude_i6, app_data + index, sizeof(int32_t));
        index += sizeof(int32_t);
        out->has_location = true;
    }

    if ((flags & kAdvertFlagHasFeature1) != 0)
    {
        if ((index + sizeof(uint16_t)) > app_data_len)
        {
            return false;
        }
        index += sizeof(uint16_t);
    }

    if ((flags & kAdvertFlagHasFeature2) != 0)
    {
        if ((index + sizeof(uint16_t)) > app_data_len)
        {
            return false;
        }
        index += sizeof(uint16_t);
    }

    if ((flags & kAdvertFlagHasName) != 0 && index < app_data_len)
    {
        const size_t name_len = app_data_len - index;
        out->has_name = copySanitizedName(out->name, sizeof(out->name),
                                          app_data + index, name_len) > 0;
    }
    out->valid = true;
    return true;
}

bool decodeDiscoverRequest(const uint8_t* payload, size_t payload_len, DecodedDiscoverRequest* out)
{
    if (!payload || !out || payload_len < 6)
    {
        return false;
    }
    if ((payload[0] & kControlSubtypeMask) != kControlSubtypeDiscoverReq)
    {
        return false;
    }

    DecodedDiscoverRequest decoded{};
    decoded.valid = true;
    decoded.prefix_only = (payload[0] & kDiscoverPrefixOnlyMask) != 0;
    decoded.type_filter = payload[1];
    memcpy(&decoded.tag, payload + 2, sizeof(decoded.tag));
    if (payload_len >= 10)
    {
        memcpy(&decoded.since, payload + 6, sizeof(decoded.since));
    }
    *out = decoded;
    return true;
}

bool buildDiscoverRequestControlPayload(const MeshCoreDiscoverRequestBuildInfo& request,
                                        uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    if (!out_payload || !out_len || out_cap < kMeshCoreDiscoverRequestBasePayloadSize)
    {
        return false;
    }

    size_t index = 0;
    out_payload[index++] = static_cast<uint8_t>(
        kControlSubtypeDiscoverReq |
        (request.prefix_only ? kDiscoverPrefixOnlyMask : 0x00));
    out_payload[index++] = request.type_filter;
    memcpy(out_payload + index, &request.tag, sizeof(request.tag));
    index += sizeof(request.tag);

    if (request.since != 0)
    {
        if (out_cap < index + sizeof(request.since))
        {
            return false;
        }
        memcpy(out_payload + index, &request.since, sizeof(request.since));
        index += sizeof(request.since);
    }

    *out_len = index;
    return true;
}

bool buildDiscoverResponseControlPayload(uint8_t node_type, int8_t snr_qdb, uint32_t tag,
                                         const uint8_t* pubkey, size_t pubkey_len,
                                         uint8_t* out_payload, size_t out_cap, size_t* out_len)
{
    if (!pubkey || pubkey_len == 0 || !out_payload || !out_len ||
        out_cap < kMeshCoreDiscoverResponseBasePayloadSize + pubkey_len)
    {
        return false;
    }

    size_t index = 0;
    out_payload[index++] = static_cast<uint8_t>(
        kControlSubtypeDiscoverResp | (node_type & 0x0F));
    out_payload[index++] = static_cast<uint8_t>(snr_qdb);
    memcpy(out_payload + index, &tag, sizeof(tag));
    index += sizeof(tag);
    memcpy(out_payload + index, pubkey, pubkey_len);
    index += pubkey_len;

    *out_len = index;
    return true;
}

bool decodeDiscoverResponse(const uint8_t* payload, size_t payload_len, DecodedDiscoverResponse* out)
{
    if (!payload || !out || payload_len < 6)
    {
        return false;
    }
    if ((payload[0] & kControlSubtypeMask) != kControlSubtypeDiscoverResp)
    {
        return false;
    }

    const size_t pubkey_len = payload_len - 6;
    if (pubkey_len < kMeshcorePubKeyPrefixSize)
    {
        return false;
    }

    DecodedDiscoverResponse decoded{};
    decoded.valid = true;
    decoded.node_type = payload[0] & 0x0F;
    decoded.snr_qdb = static_cast<int8_t>(payload[1]);
    memcpy(&decoded.tag, payload + 2, sizeof(decoded.tag));
    decoded.pubkey = payload + 6;
    decoded.pubkey_len = pubkey_len;
    *out = decoded;
    return true;
}

void formatVerificationCode(uint32_t number, char* out_code, size_t out_len)
{
    if (!out_code || out_len == 0)
    {
        return;
    }
    const uint32_t value = number % 1000000UL;
    snprintf(out_code, out_len, "%03lu %03lu",
             static_cast<unsigned long>(value / 1000UL),
             static_cast<unsigned long>(value % 1000UL));
}

} // namespace meshcore
} // namespace chat
