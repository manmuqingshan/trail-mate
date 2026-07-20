#pragma once

#include "chat/domain/mesh_peer_directory.h"

#include <cstddef>
#include <cstdint>

namespace chat::storage::v2
{

struct PeerProjection
{
    MeshPeerRecord record{};
    bool deleted = false;
};

struct ContactProjection
{
    MeshPeerIdentity identity{};
    NodeId node_id_hint = 0;
    MeshPeerUserFlags flags{};
    char alias[kMeshPeerUserAliasMaxLen + 1] = {};
    bool deleted = false;
};

std::size_t peerSlotSize(MeshProtocol protocol);
std::size_t contactSlotSize(MeshProtocol protocol);

bool encodePeerSlot(MeshProtocol protocol,
                    const PeerProjection& projection,
                    void* out,
                    std::size_t out_len);
bool decodePeerSlot(MeshProtocol protocol,
                    const void* data,
                    std::size_t len,
                    PeerProjection& out_projection);

bool encodeContactSlot(MeshProtocol protocol,
                       const ContactProjection& projection,
                       void* out,
                       std::size_t out_len);
bool decodeContactSlot(MeshProtocol protocol,
                       const void* data,
                       std::size_t len,
                       ContactProjection& out_projection);

} // namespace chat::storage::v2
