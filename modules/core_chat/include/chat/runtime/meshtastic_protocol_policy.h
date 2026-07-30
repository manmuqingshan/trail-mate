#pragma once

#include "chat/domain/chat_types.h"

#include <cstdint>

namespace chat::runtime
{

constexpr NodeId kMeshtasticBroadcastNode = 0xFFFFFFFFUL;
constexpr uint32_t kMeshtasticNodeInfoReannounceSuppressMs = 60UL * 1000UL;
constexpr uint32_t kMeshtasticNodeInfoReplySuppressMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kMeshtasticPositionReplySuppressMs = 3UL * 60UL * 1000UL;

struct MeshtasticAppDataSendPolicy
{
    NodeId wire_dest = kMeshtasticBroadcastNode;
    bool is_broadcast = true;
    bool wire_want_ack = false;
    bool track_ack = false;
    bool effective_want_response = false;
};

inline MeshtasticAppDataSendPolicy resolveMeshtasticAppDataSendPolicy(NodeId dest,
                                                                      bool want_ack,
                                                                      bool want_response)
{
    MeshtasticAppDataSendPolicy policy{};
    policy.wire_dest = (dest == 0) ? kMeshtasticBroadcastNode : dest;
    policy.is_broadcast = (policy.wire_dest == kMeshtasticBroadcastNode);
    policy.wire_want_ack = want_ack && !policy.is_broadcast;
    policy.track_ack = policy.wire_want_ack;
    policy.effective_want_response = want_response || want_ack;
    return policy;
}

enum class MeshtasticBleVisibleNameReason : uint8_t
{
    StableNodeId,
    NodeIdChanged,
};

struct MeshtasticBleVisibleNamePolicy
{
    bool visible_name_changed = false;
    MeshtasticBleVisibleNameReason reason =
        MeshtasticBleVisibleNameReason::StableNodeId;
};

inline MeshtasticBleVisibleNamePolicy resolveMeshtasticBleVisibleNamePolicy(
    NodeId previous_node,
    NodeId current_node)
{
    MeshtasticBleVisibleNamePolicy policy{};
    if (previous_node != current_node)
    {
        policy.visible_name_changed = true;
        policy.reason = MeshtasticBleVisibleNameReason::NodeIdChanged;
    }
    return policy;
}

enum class MeshtasticMqttDownlinkReason : uint8_t
{
    TransmitToMesh,
    OwnGatewayEcho,
    OwnPacket,
    TxDisabled,
    LocalDestination,
};

struct MeshtasticMqttDownlinkPolicy
{
    bool accept_locally = true;
    bool transmit_to_mesh = false;
    MeshtasticMqttDownlinkReason reason =
        MeshtasticMqttDownlinkReason::TransmitToMesh;
};

inline uint8_t meshtasticHexValue(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(c - 'a' + 10);
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(c - 'A' + 10);
    }
    return 0xFF;
}

inline bool mqttGatewayIdMatchesNode(const char* gateway_id, NodeId node_id)
{
    if (!gateway_id || gateway_id[0] != '!' || node_id == 0)
    {
        return false;
    }
    for (uint8_t i = 1; i <= 8; ++i)
    {
        if (gateway_id[i] == '\0')
        {
            return false;
        }
    }
    if (gateway_id[9] != '\0')
    {
        return false;
    }

    NodeId parsed = 0;
    for (uint8_t i = 0; i < 8; ++i)
    {
        const uint8_t value = meshtasticHexValue(gateway_id[i + 1]);
        if (value > 0x0F)
        {
            return false;
        }
        parsed = static_cast<NodeId>((parsed << 4) | value);
    }
    return parsed == node_id;
}

inline MeshtasticMqttDownlinkPolicy resolveMeshtasticMqttDownlinkPolicy(
    const char* gateway_id,
    NodeId self_node,
    NodeId packet_from,
    NodeId packet_to,
    bool tx_enabled)
{
    MeshtasticMqttDownlinkPolicy policy{};
    if (mqttGatewayIdMatchesNode(gateway_id, self_node))
    {
        policy.accept_locally = false;
        policy.reason = MeshtasticMqttDownlinkReason::OwnGatewayEcho;
        return policy;
    }
    if (self_node != 0 && packet_from == self_node)
    {
        policy.accept_locally = false;
        policy.reason = MeshtasticMqttDownlinkReason::OwnPacket;
        return policy;
    }
    if (!tx_enabled)
    {
        policy.reason = MeshtasticMqttDownlinkReason::TxDisabled;
        return policy;
    }
    if (self_node != 0 && packet_to == self_node)
    {
        policy.reason = MeshtasticMqttDownlinkReason::LocalDestination;
        return policy;
    }
    policy.transmit_to_mesh = true;
    policy.reason = MeshtasticMqttDownlinkReason::TransmitToMesh;
    return policy;
}

inline const char* meshtasticMqttDownlinkReasonName(MeshtasticMqttDownlinkReason reason)
{
    switch (reason)
    {
    case MeshtasticMqttDownlinkReason::TransmitToMesh:
        return "transmit_to_mesh";
    case MeshtasticMqttDownlinkReason::OwnGatewayEcho:
        return "own_gateway_echo";
    case MeshtasticMqttDownlinkReason::OwnPacket:
        return "own_packet";
    case MeshtasticMqttDownlinkReason::TxDisabled:
        return "tx_disabled";
    case MeshtasticMqttDownlinkReason::LocalDestination:
        return "local_destination";
    default:
        return "unknown";
    }
}

enum class MeshtasticNodeInfoReannounceReason : uint8_t
{
    Announce,
    AdapterUnavailable,
    TxDisabled,
    MqttSource,
    InvalidPeer,
    SelfPeer,
    Suppressed,
};

struct MeshtasticNodeInfoReannouncePolicy
{
    bool should_announce = false;
    MeshtasticNodeInfoReannounceReason reason =
        MeshtasticNodeInfoReannounceReason::AdapterUnavailable;
    uint32_t age_ms = 0;
};

inline MeshtasticNodeInfoReannouncePolicy resolveMeshtasticNodeInfoReannouncePolicy(
    bool adapter_ready,
    bool tx_enabled,
    bool from_mqtt,
    NodeId peer_node,
    NodeId self_node,
    uint32_t now_ms,
    uint32_t last_nodeinfo_ms,
    uint32_t suppress_ms = kMeshtasticNodeInfoReannounceSuppressMs)
{
    MeshtasticNodeInfoReannouncePolicy policy{};
    if (!adapter_ready)
    {
        policy.reason = MeshtasticNodeInfoReannounceReason::AdapterUnavailable;
        return policy;
    }
    if (!tx_enabled)
    {
        policy.reason = MeshtasticNodeInfoReannounceReason::TxDisabled;
        return policy;
    }
    if (from_mqtt)
    {
        policy.reason = MeshtasticNodeInfoReannounceReason::MqttSource;
        return policy;
    }
    if (peer_node == 0 || peer_node == kMeshtasticBroadcastNode)
    {
        policy.reason = MeshtasticNodeInfoReannounceReason::InvalidPeer;
        return policy;
    }
    if (peer_node == self_node)
    {
        policy.reason = MeshtasticNodeInfoReannounceReason::SelfPeer;
        return policy;
    }
    if (last_nodeinfo_ms != 0)
    {
        policy.age_ms = now_ms - last_nodeinfo_ms;
        if (policy.age_ms < suppress_ms)
        {
            policy.reason = MeshtasticNodeInfoReannounceReason::Suppressed;
            return policy;
        }
    }

    policy.should_announce = true;
    policy.reason = MeshtasticNodeInfoReannounceReason::Announce;
    return policy;
}

enum class MeshtasticReplyReason : uint8_t
{
    Reply,
    NoWantResponse,
    NotAddressed,
    Suppressed,
};

struct MeshtasticReplyPolicy
{
    bool should_reply = false;
    MeshtasticReplyReason reason = MeshtasticReplyReason::NoWantResponse;
    uint32_t age_ms = 0;
};

inline MeshtasticReplyPolicy resolveMeshtasticReplyPolicy(bool want_response,
                                                          bool to_us_or_broadcast,
                                                          uint32_t now_ms,
                                                          uint32_t last_reply_ms,
                                                          uint32_t suppress_ms)
{
    MeshtasticReplyPolicy policy{};
    if (!want_response)
    {
        policy.reason = MeshtasticReplyReason::NoWantResponse;
        return policy;
    }
    if (!to_us_or_broadcast)
    {
        policy.reason = MeshtasticReplyReason::NotAddressed;
        return policy;
    }
    if (last_reply_ms != 0)
    {
        policy.age_ms = now_ms - last_reply_ms;
        if (policy.age_ms < suppress_ms)
        {
            policy.reason = MeshtasticReplyReason::Suppressed;
            return policy;
        }
    }

    policy.should_reply = true;
    policy.reason = MeshtasticReplyReason::Reply;
    return policy;
}

inline MeshtasticReplyPolicy resolveMeshtasticNodeInfoReplyPolicy(
    bool want_response,
    bool to_us_or_broadcast,
    uint32_t now_ms,
    uint32_t last_reply_ms,
    uint32_t suppress_ms = kMeshtasticNodeInfoReplySuppressMs)
{
    return resolveMeshtasticReplyPolicy(
        want_response, to_us_or_broadcast, now_ms, last_reply_ms, suppress_ms);
}

inline MeshtasticReplyPolicy resolveMeshtasticPositionReplyPolicy(
    bool want_response,
    bool to_us_or_broadcast,
    uint32_t now_ms,
    uint32_t last_reply_ms,
    uint32_t suppress_ms = kMeshtasticPositionReplySuppressMs)
{
    return resolveMeshtasticReplyPolicy(
        want_response, to_us_or_broadcast, now_ms, last_reply_ms, suppress_ms);
}

enum class MeshtasticTraceRouteReplyReason : uint8_t
{
    Reply,
    ResponsePacket,
    NoWantResponse,
    NotAddressed,
    BroadcastStillInFlight,
};

struct MeshtasticTraceRouteReplyPolicy
{
    bool should_reply = false;
    MeshtasticTraceRouteReplyReason reason =
        MeshtasticTraceRouteReplyReason::ResponsePacket;
};

inline MeshtasticTraceRouteReplyPolicy resolveMeshtasticTraceRouteReplyPolicy(
    bool is_response,
    bool want_response,
    bool to_us,
    bool is_broadcast,
    uint8_t hop_limit,
    uint8_t hop_start)
{
    MeshtasticTraceRouteReplyPolicy policy{};
    if (is_response)
    {
        policy.reason = MeshtasticTraceRouteReplyReason::ResponsePacket;
        return policy;
    }
    if (!want_response)
    {
        policy.reason = MeshtasticTraceRouteReplyReason::NoWantResponse;
        return policy;
    }
    if (!to_us && !is_broadcast)
    {
        policy.reason = MeshtasticTraceRouteReplyReason::NotAddressed;
        return policy;
    }
    if (is_broadcast && hop_limit < hop_start)
    {
        policy.reason = MeshtasticTraceRouteReplyReason::BroadcastStillInFlight;
        return policy;
    }

    policy.should_reply = true;
    policy.reason = MeshtasticTraceRouteReplyReason::Reply;
    return policy;
}

} // namespace chat::runtime
