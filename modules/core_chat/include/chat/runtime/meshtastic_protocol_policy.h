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
