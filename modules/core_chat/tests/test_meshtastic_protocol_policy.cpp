#include "chat/infra/meshtastic/mt_mqtt_proxy_runtime.h"
#include "chat/runtime/meshtastic_protocol_policy.h"

#include <cassert>
#include <cstring>

int main()
{
    using chat::meshtastic::hasAnyMqttDownlinkEnabled;
    using chat::meshtastic::mqttChannelIdFor;
    using chat::meshtastic::MqttProxyRejectReason;
    using chat::meshtastic::mqttProxyRejectReasonName;
    using chat::meshtastic::mqttProxyRuntimeEnabled;
    using chat::meshtastic::MqttProxyRuntimeSettings;
    using chat::meshtastic::resolveMqttProxyDownlinkChannel;
    using chat::meshtastic::shouldPublishToMqtt;
    using chat::meshtastic::validateMqttDecodedDownlinkPayload;
    using chat::meshtastic::validateMqttDownlinkChannel;
    using chat::meshtastic::validateMqttProxyInbound;
    using chat::meshtastic::validateMqttProxyPublish;
    using chat::runtime::kMeshtasticBroadcastNode;
    using chat::runtime::MeshtasticBleVisibleNameReason;
    using chat::runtime::MeshtasticMqttDownlinkReason;
    using chat::runtime::MeshtasticNodeInfoReannounceReason;
    using chat::runtime::MeshtasticReplyReason;
    using chat::runtime::MeshtasticTraceRouteReplyReason;
    using chat::runtime::mqttGatewayIdMatchesNode;
    using chat::runtime::resolveMeshtasticAppDataSendPolicy;
    using chat::runtime::resolveMeshtasticBleVisibleNamePolicy;
    using chat::runtime::resolveMeshtasticMqttDownlinkPolicy;
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
        const auto policy = resolveMeshtasticBleVisibleNamePolicy(0x4670B90CUL,
                                                                  0x4670B90CUL);
        assert(!policy.visible_name_changed);
        assert(policy.reason == MeshtasticBleVisibleNameReason::StableNodeId);
    }

    {
        const auto policy = resolveMeshtasticBleVisibleNamePolicy(0x4670B90CUL,
                                                                  0x4670B90DUL);
        assert(policy.visible_name_changed);
        assert(policy.reason == MeshtasticBleVisibleNameReason::NodeIdChanged);
    }

    {
        assert(mqttGatewayIdMatchesNode("!A1B3B57C", 0xA1B3B57CUL));
        assert(mqttGatewayIdMatchesNode("!a1b3b57c", 0xA1B3B57CUL));
        assert(!mqttGatewayIdMatchesNode("A1B3B57C", 0xA1B3B57CUL));
        assert(!mqttGatewayIdMatchesNode("!A1B3", 0xA1B3B57CUL));
        assert(!mqttGatewayIdMatchesNode("!A1B3B57G", 0xA1B3B57CUL));
        assert(!mqttGatewayIdMatchesNode("!A1B3B57D", 0xA1B3B57CUL));
        assert(!mqttGatewayIdMatchesNode("!A1B3B57C00", 0xA1B3B57CUL));
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            nullptr, 0xA1B3B57CUL, 0x4A59CD8CUL,
            kMeshtasticBroadcastNode, true);
        assert(policy.accept_locally);
        assert(policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::TransmitToMesh);
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            "!A1B3B57C", 0xA1B3B57CUL, 0x4A59CD8CUL,
            kMeshtasticBroadcastNode, true);
        assert(!policy.accept_locally);
        assert(!policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::OwnGatewayEcho);
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            "!00000000", 0xA1B3B57CUL, 0xA1B3B57CUL,
            kMeshtasticBroadcastNode, true);
        assert(!policy.accept_locally);
        assert(!policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::OwnPacket);
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            "!00000000", 0xA1B3B57CUL, 0x4A59CD8CUL,
            0xA1B3B57CUL, true);
        assert(policy.accept_locally);
        assert(!policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::LocalDestination);
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            "!00000000", 0xA1B3B57CUL, 0x4A59CD8CUL,
            kMeshtasticBroadcastNode, true);
        assert(policy.accept_locally);
        assert(policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::TransmitToMesh);
    }

    {
        const auto policy = resolveMeshtasticMqttDownlinkPolicy(
            "!00000000", 0xA1B3B57CUL, 0x4A59CD8CUL,
            kMeshtasticBroadcastNode, false);
        assert(policy.accept_locally);
        assert(!policy.transmit_to_mesh);
        assert(policy.reason == MeshtasticMqttDownlinkReason::TxDisabled);
    }

    {
        MqttProxyRuntimeSettings settings{};
        assert(!mqttProxyRuntimeEnabled(settings));
        assert(validateMqttProxyInbound(settings, true, true) == MqttProxyRejectReason::ProxyDisabled);

        settings.enabled = true;
        settings.proxy_to_client_enabled = true;
        settings.primary_uplink_enabled = true;
        settings.primary_downlink_enabled = true;
        settings.primary_channel_id = "LongFast";
        settings.secondary_channel_id = "Secondary";

        assert(mqttProxyRuntimeEnabled(settings));
        assert(validateMqttProxyInbound(settings, false, true) == MqttProxyRejectReason::NonDataMessage);
        assert(validateMqttProxyInbound(settings, true, false) == MqttProxyRejectReason::EmptyPayload);
        assert(validateMqttProxyInbound(settings, true, true) == MqttProxyRejectReason::None);
        assert(hasAnyMqttDownlinkEnabled(settings));
        assert(validateMqttProxyPublish(settings, chat::ChannelId::PRIMARY, false, false) ==
               MqttProxyRejectReason::None);
        assert(shouldPublishToMqtt(settings, chat::ChannelId::PRIMARY, false, false));
        assert(validateMqttProxyPublish(settings, chat::ChannelId::PRIMARY, true, false) ==
               MqttProxyRejectReason::MqttLoopback);
        assert(!shouldPublishToMqtt(settings, chat::ChannelId::PRIMARY, true, false));
        assert(std::strcmp(mqttChannelIdFor(settings, chat::ChannelId::PRIMARY),
                           settings.primary_channel_id.c_str()) == 0);
        const auto primary_downlink = resolveMqttProxyDownlinkChannel(settings, "LongFast");
        assert(primary_downlink.known);
        assert(!primary_downlink.pki);
        assert(primary_downlink.channel == chat::ChannelId::PRIMARY);
        const auto secondary_downlink = resolveMqttProxyDownlinkChannel(settings, "Secondary");
        assert(!secondary_downlink.known);
        assert(!secondary_downlink.pki);
        assert(secondary_downlink.channel == chat::ChannelId::SECONDARY);
        const auto pki_downlink = resolveMqttProxyDownlinkChannel(settings, "PKI");
        assert(pki_downlink.known);
        assert(pki_downlink.pki);
        assert(validateMqttDownlinkChannel(false) == MqttProxyRejectReason::UnknownOrDisabledChannel);
        assert(validateMqttDownlinkChannel(true) == MqttProxyRejectReason::None);
        assert(validateMqttDecodedDownlinkPayload(settings,
                                                  true,
                                                  meshtastic_PortNum_TEXT_MESSAGE_APP) ==
               MqttProxyRejectReason::DecodedPayloadWhileEncrypted);
        assert(validateMqttDecodedDownlinkPayload(settings,
                                                  true,
                                                  meshtastic_PortNum_MAP_REPORT_APP) ==
               MqttProxyRejectReason::None);

        settings.encryption_enabled = false;
        assert(validateMqttDecodedDownlinkPayload(settings,
                                                  true,
                                                  meshtastic_PortNum_ADMIN_APP) ==
               MqttProxyRejectReason::AdminPayload);
        assert(validateMqttDecodedDownlinkPayload(settings,
                                                  true,
                                                  meshtastic_PortNum_TEXT_MESSAGE_APP) ==
               MqttProxyRejectReason::None);
        assert(mqttProxyRejectReasonName(MqttProxyRejectReason::UnknownOrDisabledChannel)[0] != '\0');
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
