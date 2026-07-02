#pragma once

#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/runtime/meshtastic_app_action_runtime.h"
#include "chat/runtime/meshtastic_position_core.h"
#include "chat/runtime/meshtastic_protocol_policy.h"
#include "chat/runtime/meshtastic_waypoint_core.h"
#include "chat/runtime/protocol_runtime.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_encode.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace chat::runtime
{

constexpr int32_t kMeshtasticActionDetailInvalidPeer = -1;
constexpr int32_t kMeshtasticActionDetailEncodeFailed = -2;
constexpr int32_t kMeshtasticActionDetailLocalSendFailed = -3;
constexpr int32_t kMeshtasticActionDetailInvalidInput = -4;

enum class MeshtasticPkiResyncCause : uint8_t
{
    LocalPkiNotReady = 0,
    LocalNoChannel,
    PeerKeyMissing,
    PeerKeyStale,
    PeerReportsUnknownPubkey,
    PeerReportsNoChannel,
};

struct MeshtasticPkiResyncInput
{
    MeshtasticPkiResyncCause cause = MeshtasticPkiResyncCause::PeerKeyMissing;
    NodeId peer = 0;
    MessageId request_id = 0;
    ChannelId channel = ChannelId::PRIMARY;
};

class MeshtasticPkiResyncState
{
  public:
    ProtocolEffects handle(const MeshtasticPkiResyncInput& input) const
    {
        ProtocolEffects effects{};
        if (input.peer == 0)
        {
            return effects;
        }

        if (input.cause == MeshtasticPkiResyncCause::PeerKeyStale)
        {
            ForgetPeerKeyEffect forget{};
            forget.protocol = MeshProtocol::Meshtastic;
            forget.peer = input.peer;
            effects.add(forget);
        }

        SendNodeInfoEffect node_info{};
        node_info.protocol = MeshProtocol::Meshtastic;
        node_info.channel = input.channel;
        node_info.peer = input.peer;
        node_info.want_response = true;
        effects.add(node_info);

        if (shouldReplyWithRoutingError(input.cause) && input.request_id != 0)
        {
            SendRoutingErrorEffect routing{};
            routing.protocol = MeshProtocol::Meshtastic;
            routing.channel = input.channel;
            routing.peer = input.peer;
            routing.request_id = input.request_id;
            routing.error_code = routingErrorForCause(input.cause);
            effects.add(routing);
        }

        return effects;
    }

  private:
    static bool shouldReplyWithRoutingError(MeshtasticPkiResyncCause cause)
    {
        return cause == MeshtasticPkiResyncCause::LocalPkiNotReady ||
               cause == MeshtasticPkiResyncCause::LocalNoChannel ||
               cause == MeshtasticPkiResyncCause::PeerKeyMissing ||
               cause == MeshtasticPkiResyncCause::PeerKeyStale;
    }

    static int32_t routingErrorForCause(MeshtasticPkiResyncCause cause)
    {
        return cause == MeshtasticPkiResyncCause::LocalNoChannel
                   ? meshtastic_Routing_Error_NO_CHANNEL
                   : meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY;
    }
};

class MeshtasticRuntime final : public IProtocolRuntime
{
  public:
    ProtocolEffects prepareOutgoing(const ProtocolIntent& intent,
                                    const RuntimeContext& context) override
    {
        ProtocolEffects effects{};
        std::visit(
            [this, &effects, &context](const auto& item)
            {
                using Intent = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<Intent, SendTextIntent>)
                {
                    resolveSendText(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, TraceRouteIntent>)
                {
                    resolveTraceRoute(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, ExchangePositionIntent>)
                {
                    resolveExchangePosition(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, SharePositionIntent>)
                {
                    resolveSharePosition(item, context, effects);
                }
                else if constexpr (std::is_same_v<Intent, ShareWaypointIntent>)
                {
                    resolveShareWaypoint(item, context, effects);
                }
            },
            intent);
        return effects;
    }

    ProtocolEffects handleIncoming(const IncomingPacket& packet,
                                   const RuntimeContext& context) override
    {
        return handleIncomingPacket(packet, context).effects;
    }

    IncomingPacketHandlingResult handleIncomingPacket(
        const IncomingPacket& packet,
        const RuntimeContext& context) override
    {
        IncomingPacketHandlingResult result{};
        if (packet.protocol != MeshProtocol::Meshtastic)
        {
            return result;
        }

        if (absorbIncomingHandlingResult(result, handleIncomingRoutingApp(packet, context)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingNodeInfo(packet, context)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingPosition(packet, context)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingTraceRoute(packet, context)))
        {
            return result;
        }
        if (absorbIncomingHandlingResult(result, handleIncomingKeyVerification(packet, context)))
        {
            return result;
        }
        absorbIncomingHandlingResult(result, handleIncomingTextOrAppData(packet, context));
        return result;
    }

    ProtocolEffects handleTxResult(const TxResult& result,
                                   const RuntimeContext& context) override
    {
        ProtocolEffects effects{};
        if (result.protocol != MeshProtocol::Meshtastic ||
            result.ok ||
            result.request_id == 0 ||
            !app_actions_.active())
        {
            return effects;
        }

        const MeshtasticAppActionSnapshot current = app_actions_.snapshot();
        if (current.request_id != result.request_id)
        {
            return effects;
        }

        app_actions_.markLocalSendFailed(current.kind,
                                         current.request_id,
                                         current.peer,
                                         context.now_ms);
        effects.add(actionResultFromSnapshot(app_actions_.snapshot()));
        return effects;
    }

    ProtocolEffects tick(const RuntimeContext& context) override
    {
        ProtocolEffects effects{};
        MeshtasticAppActionSnapshot snapshot{};
        if (app_actions_.tick(context.now_ms, &snapshot))
        {
            effects.add(actionResultFromSnapshot(snapshot));
        }
        return effects;
    }

    ProtocolEffects handlePkiResync(const MeshtasticPkiResyncInput& input) const
    {
        return pki_resync_.handle(input);
    }

  private:
    static constexpr uint32_t kTextRequestSalt = 0x4D545854UL;
    static constexpr uint32_t kTraceRouteRequestSalt = 0x4D545254UL;
    static constexpr uint32_t kPositionRequestSalt = 0x4D54504FUL;

    static NodeId normalizePeer(NodeId peer)
    {
        return peer == 0xFFFFFFFFUL ? 0 : peer;
    }

    static MessageId makeRequestId(MessageId requested,
                                   NodeId peer,
                                   const RuntimeContext& context,
                                   uint32_t salt)
    {
        if (requested != 0)
        {
            return requested;
        }
        MessageId id = context.now_ms ^ context.self_node ^ peer ^ salt;
        return id == 0 ? 1 : id;
    }

    static EmitActionResultEffect buildFailedAction(ProtocolActionKind action,
                                                    NodeId peer,
                                                    MessageId request_id,
                                                    int32_t detail)
    {
        EmitActionResultEffect failed{};
        failed.protocol = MeshProtocol::Meshtastic;
        failed.action = action;
        failed.state = ProtocolActionState::Failed;
        failed.peer = peer;
        failed.request_id = request_id;
        failed.detail = detail;
        return failed;
    }

    void resolveSendText(const SendTextIntent& intent,
                         const RuntimeContext& context,
                         ProtocolEffects& effects)
    {
        const NodeId peer = normalizePeer(intent.peer);
        const MessageId message_id = makeRequestId(intent.message_id,
                                                   peer,
                                                   context,
                                                   kTextRequestSalt);
        if (intent.text.empty())
        {
            effects.add(buildFailedAction(ProtocolActionKind::SendText,
                                          peer,
                                          message_id,
                                          kMeshtasticActionDetailInvalidInput));
            return;
        }

        SendTextEffect text{};
        text.protocol = MeshProtocol::Meshtastic;
        text.channel = intent.channel;
        text.peer = peer;
        text.message_id = message_id;
        text.text = intent.text;
        effects.add(std::move(text));
    }

    void resolveTraceRoute(const TraceRouteIntent& intent,
                           const RuntimeContext& context,
                           ProtocolEffects& effects)
    {
        const NodeId peer = normalizePeer(intent.peer);
        const MessageId request_id = makeRequestId(intent.request_id,
                                                   peer,
                                                   context,
                                                   kTraceRouteRequestSalt);
        if (peer == 0 || peer == context.self_node)
        {
            effects.add(buildFailedAction(ProtocolActionKind::TraceRoute,
                                          peer,
                                          request_id,
                                          kMeshtasticActionDetailInvalidPeer));
            return;
        }

        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        uint8_t route_buf[96] = {};
        pb_ostream_t stream = pb_ostream_from_buffer(route_buf, sizeof(route_buf));
        if (!pb_encode(&stream, meshtastic_RouteDiscovery_fields, &route))
        {
            effects.add(buildFailedAction(ProtocolActionKind::TraceRoute,
                                          peer,
                                          request_id,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }

        SendPacketEffect packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.channel = intent.channel;
        packet.dest = peer;
        packet.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        packet.request_id = request_id;
        packet.want_ack = true;
        packet.want_response = true;
        if (!packet.payload.assign(route_buf, stream.bytes_written))
        {
            effects.add(buildFailedAction(ProtocolActionKind::TraceRoute,
                                          peer,
                                          request_id,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }
        effects.add(std::move(packet));
        app_actions_.startTraceRoute(request_id,
                                     peer,
                                     context.now_ms,
                                     intent.timeout_ms == 0
                                         ? kMeshtasticAppActionTimeoutMs
                                         : intent.timeout_ms);
    }

    void resolveExchangePosition(const ExchangePositionIntent& intent,
                                 const RuntimeContext& context,
                                 ProtocolEffects& effects)
    {
        const NodeId peer = normalizePeer(intent.peer);
        const MessageId request_id = makeRequestId(intent.request_id,
                                                   peer,
                                                   context,
                                                   kPositionRequestSalt);
        if (peer == 0 || peer == context.self_node)
        {
            effects.add(buildFailedAction(ProtocolActionKind::ExchangePosition,
                                          peer,
                                          request_id,
                                          kMeshtasticActionDetailInvalidPeer));
            return;
        }

        SendPacketEffect packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.channel = intent.channel;
        packet.dest = peer;
        packet.portnum = meshtastic_PortNum_POSITION_APP;
        packet.request_id = request_id;
        packet.want_ack = false;
        packet.want_response = true;
        effects.add(std::move(packet));
        app_actions_.startPositionExchange(request_id,
                                           peer,
                                           context.now_ms);
    }

    static void resolveSharePosition(const SharePositionIntent& intent,
                                     const RuntimeContext& context,
                                     ProtocolEffects& effects)
    {
        (void)context;
        MeshtasticPositionInput input{};
        input.valid = intent.valid;
        input.latitude_deg = intent.latitude_deg;
        input.longitude_deg = intent.longitude_deg;
        input.has_altitude = intent.has_altitude;
        input.altitude_m = intent.altitude_m;
        input.has_speed = intent.has_speed;
        input.speed_mps = intent.speed_mps;
        input.has_course = intent.has_course;
        input.course_deg = intent.course_deg;
        input.satellites = intent.satellites;
        input.timestamp_s = intent.timestamp_s;

        uint8_t payload[meshtastic_Position_size] = {};
        size_t payload_len = sizeof(payload);
        if (!MeshtasticPositionCore::buildPositionPayload(input, payload, &payload_len))
        {
            effects.add(buildFailedAction(ProtocolActionKind::SharePosition,
                                          normalizePeer(intent.peer),
                                          0,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }

        SendPacketEffect packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.channel = intent.channel;
        packet.dest = normalizePeer(intent.peer);
        packet.portnum = meshtastic_PortNum_POSITION_APP;
        packet.want_ack = intent.want_ack && packet.dest != 0;
        packet.want_response = intent.want_response;
        if (!packet.payload.assign(payload, payload_len))
        {
            effects.add(buildFailedAction(ProtocolActionKind::SharePosition,
                                          normalizePeer(intent.peer),
                                          0,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }
        effects.add(std::move(packet));
    }

    static void resolveShareWaypoint(const ShareWaypointIntent& intent,
                                     const RuntimeContext& context,
                                     ProtocolEffects& effects)
    {
        (void)context;
        MeshtasticWaypointInput input{};
        input.valid = intent.valid;
        input.latitude_deg = intent.latitude_deg;
        input.longitude_deg = intent.longitude_deg;
        input.id = intent.id;
        input.expire = intent.expire;
        input.locked_to = intent.locked_to;
        input.icon = intent.icon;
        input.name = intent.name;
        input.description = intent.description;

        uint8_t payload[meshtastic_Waypoint_size] = {};
        size_t payload_len = sizeof(payload);
        if (!MeshtasticWaypointCore::buildWaypointPayload(input, payload, &payload_len))
        {
            effects.add(buildFailedAction(ProtocolActionKind::ShareWaypoint,
                                          normalizePeer(intent.peer),
                                          0,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }

        SendPacketEffect packet{};
        packet.protocol = MeshProtocol::Meshtastic;
        packet.channel = intent.channel;
        packet.dest = normalizePeer(intent.peer);
        packet.portnum = meshtastic_PortNum_WAYPOINT_APP;
        packet.want_ack = intent.want_ack && packet.dest != 0;
        packet.want_response = intent.want_response;
        if (!packet.payload.assign(payload, payload_len))
        {
            effects.add(buildFailedAction(ProtocolActionKind::ShareWaypoint,
                                          normalizePeer(intent.peer),
                                          0,
                                          kMeshtasticActionDetailEncodeFailed));
            return;
        }
        effects.add(std::move(packet));
    }

    static MeshIncomingData toMeshIncomingData(const IncomingPacket& packet)
    {
        MeshIncomingData data{};
        data.portnum = packet.portnum;
        data.from = packet.from;
        data.to = packet.to;
        data.packet_id = packet.packet_id;
        data.request_id = packet.request_id;
        data.channel = packet.channel;
        data.want_response = packet.want_response;
        data.payload.assign(packet.payload.begin(), packet.payload.end());
        data.rx_meta = packet.rx_meta;
        return data;
    }

    IncomingPacketHandlingResult handleIncomingRoutingApp(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        if (packet.portnum != meshtastic_PortNum_ROUTING_APP)
        {
            return {};
        }
        return consumeIncomingAppAction(packet, context);
    }

    static IncomingPacketHandlingResult handleIncomingNodeInfo(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        (void)packet;
        (void)context;
        return {};
    }

    IncomingPacketHandlingResult handleIncomingPosition(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        if (packet.portnum != meshtastic_PortNum_POSITION_APP)
        {
            return {};
        }

        IncomingPacketHandlingResult result =
            consumeIncomingAppAction(packet, context);
        if (result.shouldStop())
        {
            return result;
        }

        const auto reply_policy = resolveMeshtasticPositionReplyPolicy(
            packet.want_response,
            packetIsAddressedToUsOrBroadcast(packet, context),
            context.now_ms,
            last_position_reply_ms_);
        if (reply_policy.should_reply)
        {
            const std::size_t before = result.effects.items.size();
            appendPositionReplyEffect(result, packet, context);
            if (result.effects.items.size() != before)
            {
                last_position_reply_ms_ = context.now_ms;
            }
        }
        return result;
    }

    IncomingPacketHandlingResult handleIncomingTraceRoute(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        if (packet.portnum != meshtastic_PortNum_TRACEROUTE_APP)
        {
            return {};
        }

        ProtocolPayloadBytes updated_payload;
        if (!buildUpdatedTraceRoutePayload(packet, context, updated_payload))
        {
            return consumeIncomingAppAction(packet, context);
        }

        IncomingPacket updated_packet = packet;
        updated_packet.payload = updated_payload;

        IncomingPacketHandlingResult result =
            consumeIncomingAppAction(updated_packet, context);
        publishIncomingData(result, updated_packet, updated_payload);

        const auto reply_policy = resolveMeshtasticTraceRouteReplyPolicy(
            packet.request_id != 0,
            packet.want_response,
            packetIsToUs(packet, context),
            packetIsBroadcast(packet),
            packetHopLimit(packet),
            packetHopStart(packet));
        if (reply_policy.should_reply)
        {
            appendTraceRouteReplyEffect(result, packet, updated_payload);
        }
        return result;
    }

    static IncomingPacketHandlingResult handleIncomingKeyVerification(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        (void)packet;
        (void)context;
        return {};
    }

    static IncomingPacketHandlingResult handleIncomingTextOrAppData(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        (void)packet;
        (void)context;
        return {};
    }

    IncomingPacketHandlingResult consumeIncomingAppAction(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        IncomingPacketHandlingResult result{};
        MeshtasticAppActionSnapshot snapshot{};
        if (app_actions_.consumeIncomingPayload(packet.portnum,
                                                packet.request_id,
                                                packet.payload.empty() ? nullptr : packet.payload.data(),
                                                packet.payload.size(),
                                                context.now_ms,
                                                &snapshot))
        {
            result.handling = PacketHandling::HandledStop;
            result.effects.add(actionResultFromSnapshot(snapshot));
        }
        return result;
    }

    static ProtocolActionKind actionKindFromSnapshot(
        const MeshtasticAppActionSnapshot& snapshot)
    {
        switch (snapshot.kind)
        {
        case MeshtasticAppActionKind::TraceRoute:
            return ProtocolActionKind::TraceRoute;
        case MeshtasticAppActionKind::PositionExchange:
            return ProtocolActionKind::ExchangePosition;
        default:
            return ProtocolActionKind::Unknown;
        }
    }

    static ProtocolActionState actionStateFromSnapshot(
        const MeshtasticAppActionSnapshot& snapshot)
    {
        switch (snapshot.state)
        {
        case MeshtasticAppActionState::Delivered:
            return ProtocolActionState::Delivered;
        case MeshtasticAppActionState::Completed:
            return ProtocolActionState::Completed;
        case MeshtasticAppActionState::Failed:
            return ProtocolActionState::Failed;
        case MeshtasticAppActionState::TimedOut:
            return ProtocolActionState::TimedOut;
        case MeshtasticAppActionState::Pending:
        case MeshtasticAppActionState::Idle:
        default:
            return ProtocolActionState::Pending;
        }
    }

    static int32_t actionDetailFromSnapshot(const MeshtasticAppActionSnapshot& snapshot)
    {
        if (snapshot.reason == MeshtasticAppActionReason::LocalSendFailed)
        {
            return kMeshtasticActionDetailLocalSendFailed;
        }
        return static_cast<int32_t>(snapshot.routing_error);
    }

    static EmitActionResultEffect actionResultFromSnapshot(
        const MeshtasticAppActionSnapshot& snapshot)
    {
        EmitActionResultEffect effect{};
        effect.protocol = MeshProtocol::Meshtastic;
        effect.action = actionKindFromSnapshot(snapshot);
        effect.state = actionStateFromSnapshot(snapshot);
        effect.peer = snapshot.peer;
        effect.request_id = snapshot.request_id;
        effect.detail = actionDetailFromSnapshot(snapshot);
        return effect;
    }

    MeshtasticPkiResyncState pki_resync_{};
    MeshtasticAppActionRuntime app_actions_{};
    uint32_t last_position_reply_ms_ = 0;

    static MeshtasticPositionInput positionInputFromContext(
        const RuntimeContext& context)
    {
        MeshtasticPositionInput input{};
        input.valid = context.self_position_valid;
        input.latitude_deg = context.self_latitude_deg;
        input.longitude_deg = context.self_longitude_deg;
        input.has_altitude = context.self_has_altitude;
        input.altitude_m = context.self_altitude_m;
        input.has_speed = context.self_has_speed;
        input.speed_mps = context.self_speed_mps;
        input.has_course = context.self_has_course;
        input.course_deg = context.self_course_deg;
        input.satellites = context.self_satellites;
        input.timestamp_s = context.self_position_timestamp_s;
        return input;
    }

    static bool packetIsBroadcast(const IncomingPacket& packet)
    {
        return packet.to == kMeshtasticBroadcastNode;
    }

    static bool packetIsToUs(const IncomingPacket& packet,
                             const RuntimeContext& context)
    {
        return context.self_node != 0 && packet.to == context.self_node;
    }

    static bool packetIsAddressedToUsOrBroadcast(
        const IncomingPacket& packet,
        const RuntimeContext& context)
    {
        return packetIsToUs(packet, context) || packetIsBroadcast(packet);
    }

    static uint8_t packetWireFlags(const IncomingPacket& packet)
    {
        return packet.rx_meta.wire_flags == 0xFF ? 0 : packet.rx_meta.wire_flags;
    }

    static uint8_t packetHopLimit(const IncomingPacket& packet)
    {
        return packetWireFlags(packet) &
               chat::meshtastic::PACKET_FLAGS_HOP_LIMIT_MASK;
    }

    static uint8_t packetHopStart(const IncomingPacket& packet)
    {
        return (packetWireFlags(packet) &
                chat::meshtastic::PACKET_FLAGS_HOP_START_MASK) >>
               chat::meshtastic::PACKET_FLAGS_HOP_START_SHIFT;
    }

    static bool packetWantsAck(const IncomingPacket& packet)
    {
        return (packetWireFlags(packet) &
                chat::meshtastic::PACKET_FLAGS_WANT_ACK_MASK) != 0;
    }

    static bool copyPayloadToMeshtasticData(const IncomingPacket& packet,
                                            meshtastic_Data& data)
    {
        if (packet.payload.size() > sizeof(data.payload.bytes))
        {
            return false;
        }
        data.payload.size = static_cast<pb_size_t>(packet.payload.size());
        if (!packet.payload.empty())
        {
            std::memcpy(data.payload.bytes,
                        packet.payload.data(),
                        packet.payload.size());
        }
        return true;
    }

    static MeshIncomingData incomingDataFromPacket(
        const IncomingPacket& packet,
        const ProtocolPayloadBytes& payload)
    {
        MeshIncomingData data = toMeshIncomingData(packet);
        data.channel_hash = packet.rx_meta.channel_hash;
        data.hop_limit = packet.rx_meta.hop_limit;
        data.payload.assign(payload.begin(), payload.end());
        return data;
    }

    static void publishIncomingData(IncomingPacketHandlingResult& result,
                                    const IncomingPacket& packet,
                                    const ProtocolPayloadBytes& payload)
    {
        PublishIncomingDataEffect publish{};
        publish.data = incomingDataFromPacket(packet, payload);
        result.effects.add(std::move(publish));
        if (result.handling == PacketHandling::NotHandled)
        {
            result.handling = PacketHandling::HandledContinue;
        }
    }

    static void appendPositionReplyEffect(IncomingPacketHandlingResult& result,
                                          const IncomingPacket& packet,
                                          const RuntimeContext& context)
    {
        uint8_t payload[meshtastic_Position_size] = {};
        size_t payload_len = sizeof(payload);
        const MeshtasticPositionInput input = positionInputFromContext(context);
        if (!MeshtasticPositionCore::buildPositionPayload(input,
                                                          payload,
                                                          &payload_len))
        {
            return;
        }

        SendPacketEffect reply{};
        reply.protocol = MeshProtocol::Meshtastic;
        reply.channel = packet.channel;
        reply.dest = packet.from;
        reply.portnum = meshtastic_PortNum_POSITION_APP;
        reply.response_request_id = packet.packet_id;
        reply.want_ack = false;
        reply.want_response = false;
        if (!reply.payload.assign(payload, payload_len))
        {
            return;
        }
        result.effects.add(std::move(reply));
        if (result.handling == PacketHandling::NotHandled)
        {
            result.handling = PacketHandling::HandledContinue;
        }
    }

    static bool buildUpdatedTraceRoutePayload(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolPayloadBytes& out_payload)
    {
        meshtastic_Data decoded = meshtastic_Data_init_zero;
        decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        decoded.want_response = packet.want_response;
        decoded.request_id = packet.request_id;
        decoded.has_bitfield = true;
        decoded.bitfield = 0;
        if (!copyPayloadToMeshtasticData(packet, decoded))
        {
            return false;
        }

        meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
        if (!chat::meshtastic::updateTraceRoutePayload(
                &decoded,
                packetWireFlags(packet),
                context.self_node,
                &packet.rx_meta,
                packet.request_id != 0,
                packetIsToUs(packet, context),
                &route))
        {
            return false;
        }

        return out_payload.assign(decoded.payload.bytes, decoded.payload.size);
    }

    static void appendTraceRouteReplyEffect(IncomingPacketHandlingResult& result,
                                            const IncomingPacket& packet,
                                            const ProtocolPayloadBytes& payload)
    {
        SendPacketEffect reply{};
        reply.protocol = MeshProtocol::Meshtastic;
        reply.channel = packet.channel;
        reply.dest = packet.from;
        reply.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        reply.response_request_id = packet.packet_id;
        reply.want_ack = packetWantsAck(packet);
        reply.want_response = false;
        reply.payload = payload;
        result.effects.add(std::move(reply));
        if (result.handling == PacketHandling::NotHandled)
        {
            result.handling = PacketHandling::HandledContinue;
        }
    }
};

} // namespace chat::runtime
