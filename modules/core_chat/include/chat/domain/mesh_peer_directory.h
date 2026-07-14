#pragma once

#include "chat/domain/chat_types.h"
#include "chat/domain/reticulum_identity.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat
{

constexpr std::size_t kMeshPeerDisplayNameMaxLen = 32;
constexpr std::size_t kMeshPeerShortNameMaxLen = 10;
constexpr std::size_t kMeshPeerPublicKeyMaxLen = 64;
constexpr std::size_t kMeshPeerMeshtasticPublicKeyLen = 32;
constexpr std::size_t kMeshPeerMeshCorePublicKeyLen = 32;
constexpr std::size_t kMeshPeerReticulumPublicKeyLen = 32;
constexpr std::size_t kMeshPeerReticulumRatchetLen = 32;
constexpr std::size_t kMeshPeerMacAddrLen = 6;

enum class MeshPeerSource : uint8_t
{
    Unknown = 0,
    RuntimeRx = 1,
    DiscoveryResponse = 2,
    Manual = 3,
    Import = 4,
};

enum class MeshPeerIdentityKind : uint8_t
{
    None = 0,
    NodeId = 1,
    PublicKey = 2,
    ReticulumDestination = 3,
};

struct MeshPeerIdentity
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    MeshPeerIdentityKind kind = MeshPeerIdentityKind::None;
    NodeId node_id = 0;
    ReticulumPeerIdentity reticulum{};
    uint8_t public_key[kMeshPeerPublicKeyMaxLen] = {};
    uint8_t public_key_len = 0;
};

struct MeshPeerUserFlags
{
    bool favorite = false;
    bool ignored = false;
    bool trusted = false;
};

struct MeshPeerNodeFacts
{
    char short_name[kMeshPeerShortNameMaxLen] = {};
    char long_name[kMeshPeerDisplayNameMaxLen] = {};
    uint8_t role = 0xFF;
    uint8_t hw_model = 0;
    uint8_t channel = 0xFF;
    uint8_t hops_away = 0xFF;
    bool has_macaddr = false;
    uint8_t macaddr[kMeshPeerMacAddrLen] = {};
    bool via_mqtt = false;
};

struct MeshtasticPeerFacts
{
    MeshPeerNodeFacts node{};
    bool has_public_key = false;
    uint8_t public_key[kMeshPeerMeshtasticPublicKeyLen] = {};
    bool key_manually_verified = false;
};

struct MeshCorePeerFacts
{
    MeshPeerNodeFacts node{};
    bool has_public_key = false;
    uint8_t public_key[kMeshPeerMeshCorePublicKeyLen] = {};
    bool public_key_verified = false;
    bool has_peer_hash = false;
    uint8_t peer_hash = 0;
    bool has_next_hop = false;
    uint8_t next_hop = 0;
};

struct ReticulumPeerFacts
{
    ReticulumPeerIdentity identity{};
    bool has_public_keys = false;
    uint8_t enc_pub[kMeshPeerReticulumPublicKeyLen] = {};
    uint8_t sig_pub[kMeshPeerReticulumPublicKeyLen] = {};
    bool has_ratchet = false;
    uint8_t ratchet_pub[kMeshPeerReticulumRatchetLen] = {};
    uint32_t ratchet_seen_s = 0;
    bool delivery = false;
    bool propagation = false;
};

struct MeshPeerRecord
{
    bool valid = false;
    MeshPeerIdentity identity{};
    MeshPeerSource source = MeshPeerSource::Unknown;
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    char display_name[kMeshPeerDisplayNameMaxLen] = {};
    MeshPeerUserFlags flags{};
    MeshtasticPeerFacts meshtastic{};
    MeshCorePeerFacts meshcore{};
    ReticulumPeerFacts reticulum{};
};

inline bool meshPeerIsReticulumProtocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::Reticulum || protocol == MeshProtocol::RNode;
}

inline bool meshPeerSameProtocol(MeshProtocol lhs, MeshProtocol rhs)
{
    return lhs == rhs ||
           (meshPeerIsReticulumProtocol(lhs) && meshPeerIsReticulumProtocol(rhs));
}

inline bool meshPeerHasNonZeroBytes(const uint8_t* bytes, std::size_t len)
{
    return bytes && len > 0 && !isAllZeroKeyBytes(bytes, len);
}

inline bool meshPeerIdentityIsValid(const MeshPeerIdentity& identity)
{
    switch (identity.kind)
    {
    case MeshPeerIdentityKind::NodeId:
        return identity.node_id != 0;
    case MeshPeerIdentityKind::PublicKey:
        return identity.public_key_len > 0 &&
               identity.public_key_len <= kMeshPeerPublicKeyMaxLen &&
               meshPeerHasNonZeroBytes(identity.public_key, identity.public_key_len);
    case MeshPeerIdentityKind::ReticulumDestination:
        return identity.reticulum.valid &&
               meshPeerHasNonZeroBytes(identity.reticulum.destination_hash,
                                       kReticulumPeerHashSize);
    case MeshPeerIdentityKind::None:
    default:
        return false;
    }
}

inline bool sameMeshPeerIdentity(const MeshPeerIdentity& lhs,
                                 const MeshPeerIdentity& rhs)
{
    if (!meshPeerSameProtocol(lhs.protocol, rhs.protocol) ||
        lhs.kind != rhs.kind)
    {
        return false;
    }
    if (!meshPeerIdentityIsValid(lhs) || !meshPeerIdentityIsValid(rhs))
    {
        return false;
    }

    switch (lhs.kind)
    {
    case MeshPeerIdentityKind::NodeId:
        return lhs.node_id == rhs.node_id;
    case MeshPeerIdentityKind::PublicKey:
        return lhs.public_key_len == rhs.public_key_len &&
               std::memcmp(lhs.public_key,
                           rhs.public_key,
                           lhs.public_key_len) == 0;
    case MeshPeerIdentityKind::ReticulumDestination:
        return sameReticulumDestinationHash(lhs.reticulum, rhs.reticulum);
    case MeshPeerIdentityKind::None:
    default:
        return false;
    }
}

inline MeshPeerIdentity makeMeshPeerNodeIdentity(MeshProtocol protocol,
                                                 NodeId node_id)
{
    MeshPeerIdentity identity{};
    identity.protocol = protocol;
    identity.kind = MeshPeerIdentityKind::NodeId;
    identity.node_id = node_id;
    return identity;
}

inline MeshPeerIdentity makeMeshPeerReticulumIdentity(
    const ReticulumPeerIdentity& reticulum_identity)
{
    MeshPeerIdentity identity{};
    identity.protocol = MeshProtocol::Reticulum;
    identity.kind = MeshPeerIdentityKind::ReticulumDestination;
    identity.reticulum = reticulum_identity;
    return identity;
}

inline bool makeMeshPeerPublicKeyIdentity(MeshProtocol protocol,
                                          const uint8_t* public_key,
                                          std::size_t public_key_len,
                                          MeshPeerIdentity& out_identity)
{
    if (!public_key || public_key_len == 0 ||
        public_key_len > kMeshPeerPublicKeyMaxLen)
    {
        out_identity = MeshPeerIdentity{};
        return false;
    }
    out_identity = MeshPeerIdentity{};
    out_identity.protocol = protocol;
    out_identity.kind = MeshPeerIdentityKind::PublicKey;
    out_identity.public_key_len = static_cast<uint8_t>(public_key_len);
    std::memcpy(out_identity.public_key, public_key, public_key_len);
    return meshPeerIdentityIsValid(out_identity);
}

inline bool meshPeerRecordIsValid(const MeshPeerRecord& record)
{
    return record.valid && meshPeerIdentityIsValid(record.identity);
}

inline void copyMeshPeerText(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const char* source = text ? text : "";
    std::size_t index = 0;
    for (; index + 1 < out_len && source[index] != '\0'; ++index)
    {
        out[index] = source[index];
    }
    out[index] = '\0';
}

} // namespace chat
