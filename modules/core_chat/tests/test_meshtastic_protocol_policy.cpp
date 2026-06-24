#include "chat/runtime/meshtastic_protocol_policy.h"

#include <cassert>

int main()
{
    using chat::runtime::kMeshtasticBroadcastNode;
    using chat::runtime::MeshtasticNodeInfoReannounceReason;
    using chat::runtime::MeshtasticReplyReason;
    using chat::runtime::MeshtasticTraceRouteReplyReason;
    using chat::runtime::resolveMeshtasticAppDataSendPolicy;
    using chat::runtime::resolveMeshtasticNodeInfoReannouncePolicy;
    using chat::runtime::resolveMeshtasticNodeInfoReplyPolicy;
    using chat::runtime::resolveMeshtasticPositionReplyPolicy;
    using chat::runtime::resolveMeshtasticTraceRouteReplyPolicy;

    {
        const auto policy = resolveMeshtasticAppDataSendPolicy(0, true, true);
        assert(policy.wire_dest == kMeshtasticBroadcastNode);
        assert(policy.is_broadcast);
        assert(!policy.wire_want_ack);
        assert(!policy.track_ack);
        assert(policy.effective_want_response);
    }

    {
        const auto policy = resolveMeshtasticAppDataSendPolicy(0x12345678UL, true, false);
        assert(policy.wire_dest == 0x12345678UL);
        assert(!policy.is_broadcast);
        assert(policy.wire_want_ack);
        assert(policy.track_ack);
        assert(policy.effective_want_response);
    }

    {
        const auto policy = resolveMeshtasticAppDataSendPolicy(0x12345678UL, false, true);
        assert(!policy.wire_want_ack);
        assert(!policy.track_ack);
        assert(policy.effective_want_response);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReannouncePolicy(
            true, true, false, 0x22223333UL, 0x11112222UL, 120000, 0);
        assert(policy.should_announce);
        assert(policy.reason == MeshtasticNodeInfoReannounceReason::Announce);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReannouncePolicy(
            true, true, false, 0x22223333UL, 0x11112222UL, 120000, 90000);
        assert(!policy.should_announce);
        assert(policy.reason == MeshtasticNodeInfoReannounceReason::Suppressed);
        assert(policy.age_ms == 30000);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReannouncePolicy(
            true, true, true, 0x22223333UL, 0x11112222UL, 120000, 0);
        assert(!policy.should_announce);
        assert(policy.reason == MeshtasticNodeInfoReannounceReason::MqttSource);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReannouncePolicy(
            true, true, false, 0x11112222UL, 0x11112222UL, 120000, 0);
        assert(!policy.should_announce);
        assert(policy.reason == MeshtasticNodeInfoReannounceReason::SelfPeer);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReplyPolicy(true, true, 120000, 0);
        assert(policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::Reply);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReplyPolicy(false, true, 120000, 0);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::NoWantResponse);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReplyPolicy(true, false, 120000, 0);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::NotAddressed);
    }

    {
        const auto policy = resolveMeshtasticNodeInfoReplyPolicy(true, true, 120000, 100000);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::Suppressed);
        assert(policy.age_ms == 20000);
    }

    {
        const auto policy = resolveMeshtasticPositionReplyPolicy(true, true, 240000, 0);
        assert(policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::Reply);
    }

    {
        const auto policy = resolveMeshtasticPositionReplyPolicy(true, true, 240000, 180000);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticReplyReason::Suppressed);
        assert(policy.age_ms == 60000);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            true, true, true, false, 3, 3);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::ResponsePacket);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            false, false, true, false, 3, 3);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::NoWantResponse);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            false, true, false, false, 3, 3);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::NotAddressed);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            false, true, false, true, 2, 3);
        assert(!policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::BroadcastStillInFlight);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            false, true, true, false, 2, 3);
        assert(policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::Reply);
    }

    {
        const auto policy = resolveMeshtasticTraceRouteReplyPolicy(
            false, true, false, true, 3, 3);
        assert(policy.should_reply);
        assert(policy.reason == MeshtasticTraceRouteReplyReason::Reply);
    }

    return 0;
}
