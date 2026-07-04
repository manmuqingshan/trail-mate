#include "chat/runtime/meshcore_runtime.h"

#include <cassert>
#include <cstring>

template <typename T, typename Effects>
const T* effectAt(const Effects& effects, size_t index)
{
    if (index >= effects.items.size())
    {
        return nullptr;
    }
    return std::get_if<T>(&effects.items[index]);
}

struct CapturedIncomingResult
{
    chat::runtime::PacketHandling handling = chat::runtime::PacketHandling::NotHandled;
    chat::runtime::ProtocolEffects effects{};
};

chat::runtime::ProtocolEffects collectPrepareOutgoing(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::ProtocolIntent& intent,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.prepareOutgoing(intent, context, effects);
    return effects;
}

CapturedIncomingResult collectHandleIncomingPacket(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::IncomingPacket& packet,
    const chat::runtime::RuntimeContext& context)
{
    CapturedIncomingResult captured{};
    const auto result = runtime.handleIncomingPacket(packet, context, captured.effects);
    captured.handling = result.handling;
    return captured;
}

chat::runtime::ProtocolTxFeedbackEffects collectHandleTxResult(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::TxResult& result,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolTxFeedbackEffects effects{};
    runtime.handleTxResult(result, context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectTick(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.tick(context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectTrackAppAck(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::MeshCoreAppAckRegistration& input,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.trackAppAck(input, context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectBindAppAckToMessage(
    chat::runtime::MeshCoreRuntime& runtime,
    uint32_t signature,
    chat::MessageId message_id)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.bindAppAckToMessage(signature, message_id, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectHandleAppAck(
    chat::runtime::MeshCoreRuntime& runtime,
    uint32_t signature,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.handleAppAck(signature, context, effects);
    return effects;
}

chat::runtime::ProtocolEffects collectPrepareAutoDiscoverMissingPeer(
    chat::runtime::MeshCoreRuntime& runtime,
    const chat::runtime::MeshCoreAutoDiscoverMissingPeerInput& input,
    const chat::runtime::RuntimeContext& context)
{
    chat::runtime::ProtocolEffects effects{};
    runtime.prepareAutoDiscoverMissingPeer(input, context, effects);
    return effects;
}

int main()
{
    using chat::ChannelId;
    using chat::MeshProtocol;
    using chat::runtime::DiscoverIntent;
    using chat::runtime::EmitActionResultEffect;
    using chat::runtime::IncomingPacket;
    using chat::runtime::kMeshCoreActionDetailInvalidInput;
    using chat::runtime::kMeshCoreAutoDiscoverCooldownMs;
    using chat::runtime::kMeshCoreDiscoverRxGuardDefaultMs;
    using chat::runtime::MeshCoreAppAckRegistration;
    using chat::runtime::MeshCoreAutoDiscoverMissingPeerInput;
    using chat::runtime::MeshCoreRuntime;
    using chat::runtime::PacketHandling;
    using chat::runtime::ProtocolActionKind;
    using chat::runtime::ProtocolActionState;
    using chat::runtime::PublishNodeInfoEffect;
    using chat::runtime::RequestNodeInfoIntent;
    using chat::runtime::RuntimeContext;
    using chat::runtime::SendDiscoverRequestEffect;
    using chat::runtime::SendDiscoverResponseEffect;
    using chat::runtime::SendNodeInfoEffect;
    using chat::runtime::SendSelfAnnouncementEffect;
    using chat::runtime::SendTextEffect;
    using chat::runtime::SendTextIntent;
    using chat::runtime::SendTraceRouteEffect;
    using chat::runtime::TraceRouteIntent;
    using chat::runtime::TxResult;
    using chat::runtime::UpdatePeerRouteEffect;

    MeshCoreRuntime runtime{};
    RuntimeContext context{};
    context.protocol = MeshProtocol::MeshCore;
    context.self_node = 0x11111111UL;
    context.meshcore_discover_node_type = chat::meshcore::kMeshCoreAdvertTypeChat;
    context.meshcore_local_modified_epoch = 1781259000UL;

    {
        SendTextIntent intent{};
        intent.channel = ChannelId::SECONDARY;
        intent.peer = 0x22222222UL;
        intent.message_id = 0x01020304UL;
        intent.text = "hello meshcore";

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* text = effectAt<SendTextEffect>(effects, 0);
        assert(text);
        assert(text->protocol == MeshProtocol::MeshCore);
        assert(text->channel == intent.channel);
        assert(text->peer == intent.peer);
        assert(text->message_id == intent.message_id);
        assert(text->text == intent.text);
    }

    {
        SendTextIntent intent{};
        intent.peer = 0xFFFFFFFFUL;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(failed);
        assert(failed->protocol == MeshProtocol::MeshCore);
        assert(failed->action == ProtocolActionKind::SendText);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->peer == 0);
        assert(failed->request_id != 0);
        assert(failed->detail == kMeshCoreActionDetailInvalidInput);
    }

    {
        DiscoverIntent intent{};
        intent.action = chat::MeshDiscoveryAction::ScanLocal;
        intent.tag = 0x13572468UL;
        intent.type_filter = 0x03;
        intent.prefix_only = true;
        intent.since = 1781258481UL;
        intent.rx_guard_ms = 7000;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* discover = effectAt<SendDiscoverRequestEffect>(effects, 0);
        assert(discover);
        assert(discover->protocol == MeshProtocol::MeshCore);
        assert(discover->tag == intent.tag);
        assert(discover->type_filter == intent.type_filter);
        assert(discover->prefix_only);
        assert(discover->since == intent.since);
        assert(discover->rx_guard_ms == intent.rx_guard_ms);
    }

    {
        DiscoverIntent intent{};
        intent.action = chat::MeshDiscoveryAction::SendIdLocal;
        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* announce = effectAt<SendSelfAnnouncementEffect>(effects, 0);
        assert(announce);
        assert(announce->protocol == MeshProtocol::MeshCore);
        assert(!announce->broadcast);
    }

    {
        DiscoverIntent intent{};
        intent.action = chat::MeshDiscoveryAction::SendIdBroadcast;
        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* announce = effectAt<SendSelfAnnouncementEffect>(effects, 0);
        assert(announce);
        assert(announce->protocol == MeshProtocol::MeshCore);
        assert(announce->broadcast);
    }

    {
        MeshCoreAutoDiscoverMissingPeerInput input{};
        input.peer_hash = 0x00;
        context.now_ms = 100;
        assert(collectPrepareAutoDiscoverMissingPeer(runtime, input, context).items.empty());

        input.peer_hash = 0xFF;
        assert(collectPrepareAutoDiscoverMissingPeer(runtime, input, context).items.empty());

        input.peer_hash = static_cast<uint8_t>(context.self_node & 0xFFU);
        assert(collectPrepareAutoDiscoverMissingPeer(runtime, input, context).items.empty());
    }

    {
        MeshCoreRuntime auto_runtime{};
        RuntimeContext auto_context = context;
        auto_context.now_ms = 1000;

        MeshCoreAutoDiscoverMissingPeerInput input{};
        input.peer_hash = 0x42;
        input.self_hash = static_cast<uint8_t>(auto_context.self_node & 0xFFU);
        input.rx_guard_ms = kMeshCoreDiscoverRxGuardDefaultMs;

        const auto first = collectPrepareAutoDiscoverMissingPeer(auto_runtime, input, auto_context);
        assert(first.items.size() == 1);
        const auto* discover = effectAt<SendDiscoverRequestEffect>(first, 0);
        assert(discover);
        assert(discover->protocol == MeshProtocol::MeshCore);
        assert(discover->type_filter == chat::meshcore::kMeshCoreDiscoverTypeFilterAll);
        assert(discover->rx_guard_ms == kMeshCoreDiscoverRxGuardDefaultMs);

        auto_runtime.markAutoDiscoverMissingPeerTxResult(input.peer_hash,
                                                         auto_context,
                                                         true);
        auto_context.now_ms += kMeshCoreAutoDiscoverCooldownMs - 1;
        assert(collectPrepareAutoDiscoverMissingPeer(auto_runtime, input, auto_context).items.empty());

        auto_context.now_ms = 1000 + kMeshCoreAutoDiscoverCooldownMs;
        assert(collectPrepareAutoDiscoverMissingPeer(auto_runtime, input, auto_context).items.size() == 1);
    }

    {
        MeshCoreRuntime auto_runtime{};
        RuntimeContext auto_context = context;
        auto_context.now_ms = 2000;

        MeshCoreAutoDiscoverMissingPeerInput input{};
        input.peer_hash = 0x43;

        assert(collectPrepareAutoDiscoverMissingPeer(auto_runtime, input, auto_context).items.size() == 1);
        auto_runtime.markAutoDiscoverMissingPeerTxResult(input.peer_hash,
                                                         auto_context,
                                                         false);
        auto_context.now_ms = 2001;
        assert(collectPrepareAutoDiscoverMissingPeer(auto_runtime, input, auto_context).items.size() == 1);
    }

    {
        RequestNodeInfoIntent intent{};
        intent.peer = 0xFFFFFFFFUL;
        intent.want_response = true;
        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* node_info = effectAt<SendNodeInfoEffect>(effects, 0);
        assert(node_info);
        assert(node_info->protocol == MeshProtocol::MeshCore);
        assert(node_info->peer == 0);
        assert(node_info->want_response);
    }

    {
        chat::meshcore::MeshCoreDiscoverRequestBuildInfo request{};
        request.type_filter = static_cast<uint8_t>(1U << chat::meshcore::kMeshCoreAdvertTypeChat);
        request.prefix_only = true;
        request.tag = 0x24681357UL;
        request.since = 1781258000UL;

        uint8_t payload[chat::meshcore::kMeshCoreDiscoverRequestBasePayloadSize + sizeof(uint32_t)] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                                  payload,
                                                                  sizeof(payload),
                                                                  &payload_len));

        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.payload_type = chat::meshcore::kMeshCorePayloadTypeControl;
        packet.payload.assign(payload, payload + payload_len);

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::HandledStop);
        const auto& effects = result.effects;
        assert(effects.items.size() == 1);
        const auto* response = effectAt<SendDiscoverResponseEffect>(effects, 0);
        assert(response);
        assert(response->protocol == MeshProtocol::MeshCore);
        assert(response->tag == request.tag);
        assert(response->prefix_only);

        request.type_filter = static_cast<uint8_t>(1U << chat::meshcore::kMeshCoreAdvertTypeRepeater);
        assert(chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                                  payload,
                                                                  sizeof(payload),
                                                                  &payload_len));
        packet.payload.assign(payload, payload + payload_len);
        const auto repeater_result = collectHandleIncomingPacket(runtime, packet, context);
        assert(repeater_result.handling == PacketHandling::HandledStop);
        assert(repeater_result.effects.empty());

        request.type_filter = static_cast<uint8_t>(1U << chat::meshcore::kMeshCoreAdvertTypeChat);
        request.since = 1781260000UL;
        assert(chat::meshcore::buildDiscoverRequestControlPayload(request,
                                                                  payload,
                                                                  sizeof(payload),
                                                                  &payload_len));
        packet.payload.assign(payload, payload + payload_len);
        const auto stale_result = collectHandleIncomingPacket(runtime, packet, context);
        assert(stale_result.handling == PacketHandling::HandledStop);
        assert(stale_result.effects.empty());
    }

    {
        uint8_t pubkey[chat::meshcore::kMeshCorePubKeySize] = {};
        for (size_t i = 0; i < sizeof(pubkey); ++i)
        {
            pubkey[i] = static_cast<uint8_t>(0x42U + i);
        }

        uint8_t payload[chat::meshcore::kMeshCoreDiscoverResponseBasePayloadSize +
                        chat::meshcore::kMeshCorePubKeySize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildDiscoverResponseControlPayload(
            chat::meshcore::kMeshCoreAdvertTypeRepeater,
            6,
            0xAABBCCDDUL,
            pubkey,
            sizeof(pubkey),
            payload,
            sizeof(payload),
            &payload_len));

        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.payload_type = chat::meshcore::kMeshCorePayloadTypeControl;
        packet.payload.assign(payload, payload + payload_len);
        packet.rx_meta.rssi_dbm_x10 = -840;

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::HandledStop);
        const auto& effects = result.effects;
        assert(effects.items.size() == 2);
        const auto* publish = effectAt<PublishNodeInfoEffect>(effects, 0);
        assert(publish);
        assert(publish->protocol == MeshProtocol::MeshCore);
        assert(publish->node_id == chat::meshcore::deriveNodeIdFromPubkey(pubkey, sizeof(pubkey)));
        assert(publish->short_name == "42");
        assert(publish->long_name == "42");
        assert(publish->role == chat::meshcore::mapAdvertTypeToRole(chat::meshcore::kMeshCoreAdvertTypeRepeater));
        assert(publish->hops == 0);
        assert(publish->rx_meta.snr_db_x10 == 15);
        assert(publish->rx_meta.rssi_dbm_x10 == -840);
        assert(publish->has_public_key);

        const auto* route = effectAt<UpdatePeerRouteEffect>(effects, 1);
        assert(route);
        assert(route->protocol == MeshProtocol::MeshCore);
        assert(route->peer == publish->node_id);
        assert(route->peer_hash == 0x42);
        assert(route->public_key.size() == sizeof(pubkey));
        assert(std::memcmp(route->public_key.data(), pubkey, sizeof(pubkey)) == 0);
        assert(route->tag == 0xAABBCCDDUL);
        assert(route->payload.size() == payload_len);
    }

    {
        uint8_t payload[chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildNodeInfoQueryControlPayload(true,
                                                                payload,
                                                                sizeof(payload),
                                                                &payload_len));

        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.portnum = chat::meshcore::kMeshCoreNodeInfoPortnum;
        packet.from = 0x22222222UL;
        packet.payload.assign(payload, payload + payload_len);

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::HandledStop);
        const auto& effects = result.effects;
        assert(effects.items.size() == 1);
        const auto* reply = effectAt<SendNodeInfoEffect>(effects, 0);
        assert(reply);
        assert(reply->protocol == MeshProtocol::MeshCore);
        assert(reply->peer == packet.from);
        assert(!reply->want_response);
    }

    {
        chat::meshcore::MeshCoreNodeInfoBuildInfo info{};
        info.role = 2;
        info.hops = 9;
        info.node_id = 0x33333333UL;
        info.timestamp = 1781258481UL;
        info.short_name = "mc-node";
        info.long_name = "MeshCore Node";

        uint8_t payload[chat::meshcore::kMeshCoreNodeInfoInfoPayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildNodeInfoInfoControlPayload(info,
                                                               payload,
                                                               sizeof(payload),
                                                               &payload_len));

        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.portnum = chat::meshcore::kMeshCoreNodeInfoPortnum;
        packet.from = 0x22222222UL;
        packet.rx_meta.hop_count = 3;
        packet.payload.assign(payload, payload + payload_len);

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::HandledStop);
        const auto& effects = result.effects;
        assert(effects.items.size() == 1);
        const auto* publish = effectAt<PublishNodeInfoEffect>(effects, 0);
        assert(publish);
        assert(publish->protocol == MeshProtocol::MeshCore);
        assert(publish->node_id == info.node_id);
        assert(publish->role == info.role);
        assert(publish->hops == packet.rx_meta.hop_count);
        assert(publish->timestamp == info.timestamp);
        assert(publish->short_name == "mc-node");
        assert(publish->long_name == "MeshCore Node");
    }

    {
        TraceRouteIntent intent{};
        intent.peer = 0x22222222UL;
        intent.request_id = 0x01020304UL;
        intent.timeout_ms = 1000;
        context.now_ms = 5000;

        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        const auto* trace = effectAt<SendTraceRouteEffect>(effects, 0);
        assert(trace);
        assert(trace->protocol == MeshProtocol::MeshCore);
        assert(trace->peer == intent.peer);
        assert(trace->request_id == intent.request_id);
        assert(trace->timeout_ms == intent.timeout_ms);

        TxResult tx{};
        tx.protocol = MeshProtocol::MeshCore;
        tx.request_id = intent.request_id;
        tx.peer = intent.peer;
        tx.ok = true;
        tx.detail = 2300;
        const auto tx_effects = collectHandleTxResult(runtime, tx, context);
        assert(tx_effects.items.size() == 1);
        const auto* delivered = effectAt<EmitActionResultEffect>(tx_effects, 0);
        assert(delivered);
        assert(delivered->action == ProtocolActionKind::TraceRoute);
        assert(delivered->state == ProtocolActionState::Delivered);
        assert(delivered->request_id == intent.request_id);

        uint8_t payload[chat::meshcore::kMeshCoreTraceBasePayloadSize] = {};
        size_t payload_len = 0;
        assert(chat::meshcore::buildTracePayload(intent.request_id,
                                                 0,
                                                 0,
                                                 payload,
                                                 sizeof(payload),
                                                 &payload_len));

        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.payload_type = chat::meshcore::kMeshCorePayloadTypeTrace;
        packet.payload.assign(payload, payload + payload_len);
        packet.path.assign({10, 11});
        const auto rx_result = collectHandleIncomingPacket(runtime, packet, context);
        assert(rx_result.handling == PacketHandling::HandledStop);
        const auto& rx_effects = rx_result.effects;
        assert(rx_effects.items.size() == 1);
        const auto* completed = effectAt<EmitActionResultEffect>(rx_effects, 0);
        assert(completed);
        assert(completed->action == ProtocolActionKind::TraceRoute);
        assert(completed->state == ProtocolActionState::Completed);
        assert(completed->peer == intent.peer);
        assert(completed->request_id == intent.request_id);
        assert(completed->detail == 2);
    }

    {
        TraceRouteIntent intent{};
        intent.peer = 0x44444444UL;
        intent.request_id = 0x0A0B0C0DUL;
        intent.timeout_ms = 100;
        context.now_ms = 1000;
        const auto effects = collectPrepareOutgoing(runtime, intent, context);
        assert(effects.items.size() == 1);
        assert(effectAt<SendTraceRouteEffect>(effects, 0));

        context.now_ms = 1099;
        assert(collectTick(runtime, context).items.empty());

        context.now_ms = 1100;
        const auto timeout_effects = collectTick(runtime, context);
        assert(timeout_effects.items.size() == 1);
        const auto* timed_out = effectAt<EmitActionResultEffect>(timeout_effects, 0);
        assert(timed_out);
        assert(timed_out->action == ProtocolActionKind::TraceRoute);
        assert(timed_out->state == ProtocolActionState::TimedOut);
        assert(timed_out->peer == intent.peer);
        assert(timed_out->request_id == intent.request_id);
    }

    {
        IncomingPacket packet{};
        packet.protocol = MeshProtocol::MeshCore;
        packet.portnum = 0xFEEDUL;
        packet.payload_type = 0x01;
        packet.payload.push_back(0xAA);

        const auto result = collectHandleIncomingPacket(runtime, packet, context);
        assert(result.handling == PacketHandling::NotHandled);
        assert(result.effects.empty());
    }

    {
        MeshCoreAppAckRegistration ack{};
        ack.signature = 0xABCDEF01UL;
        ack.peer = 0x55555555UL;
        ack.portnum = 0x1001;
        ack.timeout_ms = 500;
        context.now_ms = 2000;
        assert(collectTrackAppAck(runtime, ack, context).items.empty());
        assert(collectBindAppAckToMessage(runtime, ack.signature, 77).items.empty());

        context.now_ms = 2075;
        const auto effects = collectHandleAppAck(runtime, ack.signature, context);
        assert(effects.items.size() == 1);
        const auto* completed = effectAt<EmitActionResultEffect>(effects, 0);
        assert(completed);
        assert(completed->protocol == MeshProtocol::MeshCore);
        assert(completed->action == ProtocolActionKind::SendText);
        assert(completed->state == ProtocolActionState::Completed);
        assert(completed->peer == ack.peer);
        assert(completed->request_id == ack.signature);
        assert(completed->message_id == 77);
        assert(completed->detail == 75);
        assert(collectHandleAppAck(runtime, ack.signature, context).items.empty());
    }

    {
        MeshCoreAppAckRegistration ack{};
        ack.signature = 0xABCDEF02UL;
        ack.peer = 0x66666666UL;
        ack.message_id = 88;
        ack.timeout_ms = 100;
        context.now_ms = 3000;
        assert(collectTrackAppAck(runtime, ack, context).items.empty());

        context.now_ms = 3099;
        assert(collectTick(runtime, context).items.empty());

        context.now_ms = 3100;
        const auto effects = collectTick(runtime, context);
        assert(effects.items.size() == 1);
        const auto* timed_out = effectAt<EmitActionResultEffect>(effects, 0);
        assert(timed_out);
        assert(timed_out->action == ProtocolActionKind::SendText);
        assert(timed_out->state == ProtocolActionState::TimedOut);
        assert(timed_out->peer == ack.peer);
        assert(timed_out->request_id == ack.signature);
        assert(timed_out->message_id == ack.message_id);
        assert(timed_out->detail == 100);
    }

    {
        MeshCoreRuntime ack_runtime{};
        context.now_ms = 4000;
        for (uint32_t index = 0; index < 32; ++index)
        {
            MeshCoreAppAckRegistration ack{};
            ack.signature = 0xC0000000UL + index;
            ack.peer = 0x70000000UL + index;
            ack.message_id = 1000 + index;
            ack.timeout_ms = 5000;
            assert(collectTrackAppAck(ack_runtime, ack, context).items.empty());
        }

        MeshCoreAppAckRegistration overflow{};
        overflow.signature = 0xC0000020UL;
        overflow.peer = 0x70000020UL;
        overflow.message_id = 2000;
        overflow.timeout_ms = 5000;
        const auto dropped = collectTrackAppAck(ack_runtime, overflow, context);
        assert(dropped.items.size() == 1);
        const auto* failed = effectAt<EmitActionResultEffect>(dropped, 0);
        assert(failed);
        assert(failed->action == ProtocolActionKind::SendText);
        assert(failed->state == ProtocolActionState::Failed);
        assert(failed->request_id == 0xC0000000UL);
        assert(failed->message_id == 1000);

        assert(collectHandleAppAck(ack_runtime, 0xC0000000UL, context).items.empty());

        context.now_ms = 4075;
        const auto completed_effects = collectHandleAppAck(ack_runtime, overflow.signature, context);
        assert(completed_effects.items.size() == 1);
        const auto* completed = effectAt<EmitActionResultEffect>(completed_effects, 0);
        assert(completed);
        assert(completed->state == ProtocolActionState::Completed);
        assert(completed->peer == overflow.peer);
        assert(completed->request_id == overflow.signature);
        assert(completed->message_id == overflow.message_id);
        assert(completed->detail == 75);
    }

    return 0;
}
