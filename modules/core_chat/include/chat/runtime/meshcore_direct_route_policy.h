#pragma once

#include "chat/domain/chat_types.h"

#include <cstdint>

namespace chat::runtime
{

enum class MeshCoreDirectRouteStatus : uint8_t
{
    Ready,
    MissingPeerPublicKey,
};

enum class MeshCoreRouteMode : uint8_t
{
    Flood,
    Direct,
};

struct MeshCoreDirectRouteFacts
{
    bool identity_ready = false;
    bool has_peer_route = false;
    bool peer_has_public_key = false;
    bool has_selected_route = false;
    ChannelId requested_channel = ChannelId::PRIMARY;
    ChannelId selected_route_channel = ChannelId::PRIMARY;
};

struct MeshCoreDirectRouteDecision
{
    MeshCoreDirectRouteStatus status = MeshCoreDirectRouteStatus::Ready;
    MeshCoreRouteMode route_mode = MeshCoreRouteMode::Flood;
    ChannelId tx_channel = ChannelId::PRIMARY;
    bool should_discover = false;
};

inline MeshCoreDirectRouteDecision resolveMeshCoreDirectRoutePolicy(
    const MeshCoreDirectRouteFacts& facts)
{
    MeshCoreDirectRouteDecision decision{};
    decision.tx_channel = facts.requested_channel;

    if (facts.identity_ready && (!facts.has_peer_route || !facts.peer_has_public_key))
    {
        decision.status = MeshCoreDirectRouteStatus::MissingPeerPublicKey;
        decision.should_discover = true;
        return decision;
    }

    if (facts.has_selected_route)
    {
        decision.route_mode = MeshCoreRouteMode::Direct;
        decision.tx_channel = facts.selected_route_channel;
    }

    return decision;
}

} // namespace chat::runtime
