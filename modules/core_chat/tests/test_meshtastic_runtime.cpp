#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/runtime/meshtastic_runtime.h"

#include "pb_decode.h"
#include "pb_encode.h"

#include <cassert>
#include <string>
#include <vector>

namespace
{

template <typename T>
const T* effectAt(const chat::runtime::ProtocolEffects& effects, size_t index)
{
    assert(index < effects.items.size());
    return std::get_if<T>(&effects.items[index]);
}

std::vector<uint8_t> encodeRouting(meshtastic_Routing_Error reason)
{
    meshtastic_Routing routing = meshtastic_Routing_init_zero;
    routing.which_variant = meshtastic_Routing_error_reason_tag;
    routing.error_reason = reason;

    uint8_t buffer[32] = {};
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
    assert(pb_encode(&stream, meshtastic_Routing_fields, &routing));
    return std::vector<uint8_t>(buffer, buffer + stream.bytes_written);
}

} // namespace

int main()
{
    using chat::ChannelId;
    using chat::MeshProtocol;
    using chat::runtime::EmitActionResultEffect;
    using chat::runtime::ExchangePositionIntent;
    using chat::runtime::ForgetPeerKeyEffect;
    using chat::runtime::IncomingPacket;
    using chat::runtime::kMeshtasticActionDetailInvalidInput;
    using chat::runtime::kMeshtasticActionDetailLocalSendFailed;
    using chat::runtime::kMeshtasticAppActionTimeoutMs;
    using chat::runtime::MeshtasticPkiResyncCause;
    using chat::runtime::MeshtasticPkiResyncInput;
    using chat::runtime::MeshtasticRuntime;
    using chat::runtime::PacketHandling;
    using chat::runtime::ProtocolActionKind;
    using chat::runtime::ProtocolActionState;
    using chat::runtime::PublishIncomingDataEffect;
    using chat::runtime::RuntimeContext;
    using chat::runtime::SendNodeInfoEffect;
    using chat::runtime::SendPacketEffect;
    using chat::runtime::SendRoutingErrorEffect;
    using chat::runtime::SendTextEffect;
    using chat::runtime::SendTextIntent;
    using chat::runtime::SharePositionIntent;
    using chat::runtime::ShareWaypointIntent;
    using chat::runtime::TraceRouteIntent;
    using chat::runtime::TxResult;

    MeshtasticRuntime runtime;
    RuntimeContext context{};
    context.protocol = MeshProtocol::Meshtastic;
    context.self_node = 0x11111111UL;
    context.now_ms = 0x20240614UL;

    {
        SendTextIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x22222222UL;
        intent.message_id = 0x01020304UL;
        intent.text = "hello runtime";

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* text = effectAt<SendTextEffect>(effects, 0);
        assert(text);
        assert(text->protocol == MeshProtocol::Meshtastic);
        assert(text->channel == intent.channel);
        assert(text->peer == intent.peer);
        assert(text->message_id == intent.message_id);
        assert(text->text == intent.text);
    }

    {
        SendTextIntent intent{};
        intent.peer = 0xFFFFFFFFUL;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::SendText);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == 0);
        assert(failed->request_id != 0);
        assert(failed->detail == kMeshtasticActionDetailInvalidInput);
    }

    {
        TraceRouteIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x22222222UL;
        intent.request_id = 0x01020304UL;
        intent.timeout_ms = 9000;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(packet->request_id == intent.request_id);
        assert(packet->want_ack);
        assert(packet->want_response);

        meshtastic_RouteDiscovery decoded = meshtastic_RouteDiscovery_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_RouteDiscovery_fields, &decoded));
    }

    {
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        decoded.want_response = true;
        decoded.has_bitfield = true;
        decoded.bitfield = 0;
        decoded.payload.size = 0;

        chat::RxMeta rx_meta{};
        rx_meta.snr_db_x10 = 58;

        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        assert(chat::meshtastic::updateTraceRoutePayload(&decoded,
                                                         0x4A,
                                                         0xA1B3B57CUL,
                                                         &rx_meta,
                                                         false,
                                                         true,
                                                         &route));
        assert(decoded.payload.size > 0);
        assert(route.route_count == 0);
        assert(route.snr_towards_count == 1);
        assert(route.snr_towards[0] == 23);
    }

    {
        ExchangePositionIntent intent{};
        intent.channel = ChannelId::PRIMARY;
        intent.peer = 0x33333333UL;
        intent.request_id = 0x05060708UL;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_POSITION_APP);
        assert(packet->request_id == intent.request_id);
        assert(!packet->want_ack);
        assert(packet->want_response);
        assert(packet->payload.empty());
    }

    {
        MeshtasticRuntime reply_runtime;
        RuntimeContext reply_context = context;
        reply_context.self_node = 0xA1B3B57CUL;
        reply_context.now_ms = 10000;

        IncomingPacket request{};
        request.protocol = MeshProtocol::Meshtastic;
        request.channel = ChannelId::PRIMARY;
        request.from = 0xE2A7B711UL;
        request.to = reply_context.self_node;
        request.packet_id = 0x9578D958UL;
        request.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        request.want_response = true;
        request.rx_meta.wire_flags = 0x4A;
        request.rx_meta.snr_db_x10 = 58;

        const auto result = reply_runtime.handleIncomingPacket(request, reply_context);
        assert(result.handling == PacketHandling::HandledContinue);
        assert(result.effects.items.size() == 2);

        const auto* publish = effectAt<PublishIncomingDataEffect>(result.effects, 0);
        assert(publish);
        assert(publish->data.portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(publish->data.from == request.from);
        assert(publish->data.to == request.to);
        assert(publish->data.packet_id == request.packet_id);
        assert(!publish->data.payload.empty());

        const auto* reply = effectAt<SendPacketEffect>(result.effects, 1);
        assert(reply);
        assert(reply->protocol == MeshProtocol::Meshtastic);
        assert(reply->channel == request.channel);
        assert(reply->dest == request.from);
        assert(reply->portnum == meshtastic_PortNum_TRACEROUTE_APP);
        assert(reply->request_id == 0);
        assert(reply->response_request_id == request.packet_id);
        assert(!reply->want_response);
        assert(reply->payload == publish->data.payload);
    }

    {
        MeshtasticRuntime reply_runtime;
        RuntimeContext reply_context = context;
        reply_context.self_node = 0xA1B3B57CUL;
        reply_context.now_ms = 11000;
        reply_context.self_position_valid = true;
        reply_context.self_latitude_deg = 26.67773;
        reply_context.self_longitude_deg = 107.28225;
        reply_context.self_has_altitude = true;
        reply_context.self_altitude_m = 1903.6;
        reply_context.self_position_timestamp_s = 1710001234U;

        IncomingPacket request{};
        request.protocol = MeshProtocol::Meshtastic;
        request.channel = ChannelId::SECONDARY;
        request.from = 0xE2A7B711UL;
        request.to = reply_context.self_node;
        request.packet_id = 0x69112BFEUL;
        request.portnum = meshtastic_PortNum_POSITION_APP;
        request.want_response = true;

        const auto result = reply_runtime.handleIncomingPacket(request, reply_context);
        assert(result.handling == PacketHandling::HandledContinue);
        assert(result.effects.items.size() == 1);

        const auto* reply = effectAt<SendPacketEffect>(result.effects, 0);
        assert(reply);
        assert(reply->protocol == MeshProtocol::Meshtastic);
        assert(reply->channel == request.channel);
        assert(reply->dest == request.from);
        assert(reply->portnum == meshtastic_PortNum_POSITION_APP);
        assert(reply->request_id == 0);
        assert(reply->response_request_id == request.packet_id);
        assert(!reply->want_ack);
        assert(!reply->want_response);
        assert(!reply->payload.empty());

        meshtastic_Position decoded = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(reply->payload.data(),
                                                     reply->payload.size());
        assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(reply_context.self_latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(reply_context.self_longitude_deg * 1e7));
        assert(decoded.has_altitude);
        assert(decoded.altitude == 1904);
        assert(decoded.timestamp == reply_context.self_position_timestamp_s);
    }

    {
        SharePositionIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x44444444UL;
        intent.valid = true;
        intent.latitude_deg = 26.67773;
        intent.longitude_deg = 107.28225;
        intent.has_altitude = true;
        intent.altitude_m = 1903.6;
        intent.timestamp_s = 1710000000U;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_POSITION_APP);
        assert(!packet->want_ack);
        assert(!packet->want_response);
        assert(!packet->payload.empty());

        meshtastic_Position decoded = meshtastic_Position_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_Position_fields, &decoded));
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(intent.latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(intent.longitude_deg * 1e7));
        assert(decoded.has_altitude);
        assert(decoded.altitude == 1904);
        assert(decoded.timestamp == intent.timestamp_s);
    }

    {
        SharePositionIntent intent{};
        intent.peer = 0x55555555UL;
        intent.valid = false;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::SharePosition);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == intent.peer);
    }

    {
        ShareWaypointIntent intent{};
        intent.channel = ChannelId::PRIMARY;
        intent.peer = 0x66666666UL;
        intent.valid = true;
        intent.latitude_deg = 26.67773;
        intent.longitude_deg = 107.28225;
        intent.id = 1710000000U;
        intent.expire = intent.id + 86400U;
        intent.name = "Trail Mate POI";
        intent.description = "Shared from uConsole current GPS fix";

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* packet = effectAt<SendPacketEffect>(effects, 0);
        assert(packet);
        assert(packet->protocol == MeshProtocol::Meshtastic);
        assert(packet->channel == intent.channel);
        assert(packet->dest == intent.peer);
        assert(packet->portnum == meshtastic_PortNum_WAYPOINT_APP);
        assert(!packet->want_ack);
        assert(!packet->want_response);
        assert(!packet->payload.empty());

        meshtastic_Waypoint decoded = meshtastic_Waypoint_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(packet->payload.data(),
                                                     packet->payload.size());
        assert(pb_decode(&stream, meshtastic_Waypoint_fields, &decoded));
        assert(decoded.id == intent.id);
        assert(decoded.has_latitude_i);
        assert(decoded.latitude_i == static_cast<int32_t>(intent.latitude_deg * 1e7));
        assert(decoded.has_longitude_i);
        assert(decoded.longitude_i == static_cast<int32_t>(intent.longitude_deg * 1e7));
        assert(decoded.expire == intent.expire);
        assert(std::string(decoded.name) == intent.name);
        assert(std::string(decoded.description) == intent.description);
    }

    {
        ShareWaypointIntent intent{};
        intent.peer = 0x77777777UL;
        intent.valid = false;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::ShareWaypoint);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == intent.peer);
    }

    {
        TraceRouteIntent intent{};
        intent.peer = context.self_node;
        intent.request_id = 0x0A0B0C0DUL;

        const auto effects = runtime.prepareOutgoing(intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::Meshtastic);
        assert(failed->action == ProtocolActionKind::TraceRoute);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == context.self_node);
        assert(failed->request_id == intent.request_id);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 1000;

        TraceRouteIntent intent{};
        intent.peer = 0x24242424UL;
        intent.request_id = 0x6006UL;
        intent.timeout_ms = 5000;
        assert(action_runtime.prepareOutgoing(intent, action_context).items.size() == 1);

        IncomingPacket routing{};
        routing.protocol = MeshProtocol::Meshtastic;
        routing.portnum = meshtastic_PortNum_ROUTING_APP;
        routing.request_id = intent.request_id;
        const auto routing_payload = encodeRouting(meshtastic_Routing_Error_NONE);
        assert(routing.payload.assign(routing_payload));
        action_context.now_ms = 1200;

        const auto delivered_result = action_runtime.handleIncomingPacket(routing, action_context);
        assert(delivered_result.handling == PacketHandling::HandledStop);
        const auto& delivered_effects = delivered_result.effects;
        assert(delivered_effects.items.size() == 1);
        const auto* delivered = effectAt<EmitActionResultEffect>(delivered_effects, 0);
        assert(delivered);
        assert(delivered->action == ProtocolActionKind::TraceRoute);
        assert(delivered->state == ProtocolActionState::Delivered);
        assert(delivered->request_id == intent.request_id);

        IncomingPacket response{};
        response.protocol = MeshProtocol::Meshtastic;
        response.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        response.request_id = intent.request_id;
        response.payload.push_back(0);
        action_context.now_ms = 1500;

        const auto completed_result = action_runtime.handleIncomingPacket(response, action_context);
        assert(completed_result.handling == PacketHandling::HandledStop);
        const auto& completed_effects = completed_result.effects;
        assert(completed_effects.items.size() == 1);
        const auto* completed = effectAt<EmitActionResultEffect>(completed_effects, 0);
        assert(completed);
        assert(completed->action == ProtocolActionKind::TraceRoute);
        assert(completed->state == ProtocolActionState::Completed);
        assert(completed->request_id == intent.request_id);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 2000;

        ExchangePositionIntent intent{};
        intent.peer = 0x35353535UL;
        intent.request_id = 0x7007UL;
        assert(action_runtime.prepareOutgoing(intent, action_context).items.size() == 1);

        TxResult tx{};
        tx.protocol = MeshProtocol::Meshtastic;
        tx.request_id = intent.request_id;
        tx.ok = false;
        action_context.now_ms = 2100;

        const auto failed_effects = action_runtime.handleTxResult(tx, action_context);
        assert(failed_effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(failed_effects, 0);
        assert(failed);
        assert(failed->action == ProtocolActionKind::ExchangePosition);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->detail == kMeshtasticActionDetailLocalSendFailed);
    }

    {
        MeshtasticRuntime action_runtime;
        RuntimeContext action_context = context;
        action_context.now_ms = 3000;

        ExchangePositionIntent intent{};
        intent.peer = 0x46464646UL;
        intent.request_id = 0x8008UL;
        assert(action_runtime.prepareOutgoing(intent, action_context).items.size() == 1);

        action_context.now_ms = 3000 + kMeshtasticAppActionTimeoutMs - 1;
        assert(action_runtime.tick(action_context).items.empty());

        action_context.now_ms = 3000 + kMeshtasticAppActionTimeoutMs;
        const auto timeout_effects = action_runtime.tick(action_context);
        assert(timeout_effects.items.size() == 1);
        const auto* timed_out = effectAt<EmitActionResultEffect>(timeout_effects, 0);
        assert(timed_out);
        assert(timed_out->action == ProtocolActionKind::ExchangePosition);
        assert(timed_out->state == ProtocolActionState::TimedOut);
    }

    {
        IncomingPacket packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.portnum = 0xFEEDUL;

        const auto result = runtime.handleIncomingPacket(packet, context);
        assert(result.handling == PacketHandling::NotHandled);
        assert(result.effects.empty());
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyMissing;
        input.peer = 0xAABBCCDDUL;
        input.request_id = 0x1001UL;
        input.channel = ChannelId::PRIMARY;

        const auto effects = runtime.handlePkiResync(input);
        assert(effects.items.size() == 2);

        const auto* node_info = effectAt<SendNodeInfoEffect>(effects, 0);
        assert(node_info);
        assert(node_info->protocol == MeshProtocol::Meshtastic);
        assert(node_info->peer == input.peer);
        assert(node_info->want_response);

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 1);
        assert(routing);
        assert(routing->peer == input.peer);
        assert(routing->request_id == input.request_id);
        assert(routing->error_code == meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyStale;
        input.peer = 0x01020304UL;
        input.request_id = 0x2002UL;
        input.channel = ChannelId::SECONDARY;

        const auto effects = runtime.handlePkiResync(input);
        assert(effects.items.size() == 3);

        const auto* forget = effectAt<ForgetPeerKeyEffect>(effects, 0);
        assert(forget);
        assert(forget->peer == input.peer);

        const auto* node_info = effectAt<SendNodeInfoEffect>(effects, 1);
        assert(node_info);
        assert(node_info->channel == ChannelId::SECONDARY);

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 2);
        assert(routing);
        assert(routing->channel == ChannelId::SECONDARY);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerReportsUnknownPubkey;
        input.peer = 0x0BADF00DUL;
        input.request_id = 0x3003UL;

        const auto effects = runtime.handlePkiResync(input);
        assert(effects.items.size() == 1);
        assert(effectAt<SendNodeInfoEffect>(effects, 0));
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::LocalNoChannel;
        input.peer = 0x11223344UL;
        input.request_id = 0x5005UL;

        const auto effects = runtime.handlePkiResync(input);
        assert(effects.items.size() == 2);
        assert(effectAt<SendNodeInfoEffect>(effects, 0));

        const auto* routing = effectAt<SendRoutingErrorEffect>(effects, 1);
        assert(routing);
        assert(routing->peer == input.peer);
        assert(routing->request_id == input.request_id);
        assert(routing->error_code == meshtastic_Routing_Error_NO_CHANNEL);
    }

    {
        MeshtasticPkiResyncInput input{};
        input.cause = MeshtasticPkiResyncCause::PeerKeyMissing;
        input.peer = 0;
        input.request_id = 0x4004UL;

        const auto effects = runtime.handlePkiResync(input);
        assert(effects.empty());
    }

    return 0;
}
