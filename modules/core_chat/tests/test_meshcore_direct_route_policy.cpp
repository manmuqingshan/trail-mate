#include "chat/runtime/meshcore_direct_route_policy.h"

#include <cassert>

int main()
{
    {
        chat::runtime::MeshCoreDirectRouteFacts facts{};
        facts.identity_ready = true;
        facts.has_peer_route = false;

        const auto decision = chat::runtime::resolveMeshCoreDirectRoutePolicy(facts);
        assert(decision.status == chat::runtime::MeshCoreDirectRouteStatus::MissingPeerPublicKey);
        assert(decision.should_discover);
    }

    {
        chat::runtime::MeshCoreDirectRouteFacts facts{};
        facts.identity_ready = true;
        facts.has_peer_route = true;
        facts.peer_has_public_key = true;
        facts.has_selected_route = false;
        facts.requested_channel = chat::ChannelId::SECONDARY;

        const auto decision = chat::runtime::resolveMeshCoreDirectRoutePolicy(facts);
        assert(decision.status == chat::runtime::MeshCoreDirectRouteStatus::Ready);
        assert(decision.route_mode == chat::runtime::MeshCoreRouteMode::Flood);
        assert(decision.tx_channel == chat::ChannelId::SECONDARY);
    }

    {
        chat::runtime::MeshCoreDirectRouteFacts facts{};
        facts.identity_ready = true;
        facts.has_peer_route = true;
        facts.peer_has_public_key = true;
        facts.has_selected_route = true;
        facts.requested_channel = chat::ChannelId::PRIMARY;
        facts.selected_route_channel = chat::ChannelId::SECONDARY;

        const auto decision = chat::runtime::resolveMeshCoreDirectRoutePolicy(facts);
        assert(decision.status == chat::runtime::MeshCoreDirectRouteStatus::Ready);
        assert(decision.route_mode == chat::runtime::MeshCoreRouteMode::Direct);
        assert(decision.tx_channel == chat::ChannelId::SECONDARY);
    }

    return 0;
}
