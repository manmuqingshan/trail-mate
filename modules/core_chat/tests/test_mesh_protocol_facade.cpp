#include "chat/runtime/mesh_protocol_facade.h"

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

class StaticContextProvider final : public chat::runtime::IProtocolRuntimeContextProvider
{
  public:
    chat::runtime::RuntimeContext runtimeContext() const override
    {
        return context;
    }

    chat::runtime::RuntimeContext context{};
};

class FakeRuntime final : public chat::runtime::IProtocolRuntime
{
  public:
    chat::runtime::ProtocolEffects prepareOutgoing(
        const chat::runtime::ProtocolIntent& intent,
        const chat::runtime::RuntimeContext& context) override
    {
        ++prepare_count;
        last_context = context;

        chat::runtime::ProtocolEffects effects{};
        if (const auto* trace = std::get_if<chat::runtime::TraceRouteIntent>(&intent))
        {
            saw_trace_route = true;
            last_peer = trace->peer;

            chat::runtime::SendPacketEffect packet{};
            packet.protocol = context.protocol;
            packet.channel = trace->channel;
            packet.dest = trace->peer;
            packet.request_id = next_request_id;
            packet.want_ack = true;
            packet.want_response = true;
            packet.payload.assign({0x01, 0x02, 0x03});
            effects.add(packet);
        }
        else if (const auto* text = std::get_if<chat::runtime::SendTextIntent>(&intent))
        {
            saw_send_text = true;
            last_peer = text->peer;
            last_text = text->text;

            chat::runtime::SendTextEffect effect{};
            effect.protocol = context.protocol;
            effect.channel = text->channel;
            effect.peer = text->peer;
            effect.message_id = text->message_id != 0 ? text->message_id : next_request_id;
            effect.text = text->text;
            effects.add(effect);
        }
        else if (const auto* node_info =
                     std::get_if<chat::runtime::RequestNodeInfoIntent>(&intent))
        {
            saw_request_node_info = true;
            last_peer = node_info->peer;

            chat::runtime::SendNodeInfoEffect node{};
            node.protocol = context.protocol;
            node.peer = node_info->peer;
            node.want_response = node_info->want_response;
            effects.add(node);
        }
        else if (const auto* position =
                     std::get_if<chat::runtime::SharePositionIntent>(&intent))
        {
            saw_share_position = true;
            last_peer = position->peer;

            chat::runtime::SendPacketEffect packet{};
            packet.protocol = context.protocol;
            packet.channel = position->channel;
            packet.dest = position->peer;
            packet.request_id = next_request_id;
            effects.add(packet);
        }
        return effects;
    }

    chat::runtime::ProtocolEffects handleIncoming(
        const chat::runtime::IncomingPacket& packet,
        const chat::runtime::RuntimeContext& context) override
    {
        ++incoming_count;
        last_context = context;
        last_peer = packet.from;

        chat::runtime::ProtocolEffects effects{};
        chat::runtime::PublishIncomingDataEffect data{};
        data.data.from = packet.from;
        data.data.to = packet.to;
        data.data.portnum = packet.portnum;
        data.data.payload = packet.payload;
        effects.add(data);
        return effects;
    }

    chat::runtime::ProtocolEffects handleTxResult(
        const chat::runtime::TxResult& result,
        const chat::runtime::RuntimeContext& context) override
    {
        ++tx_result_count;
        last_context = context;
        last_tx = result;

        chat::runtime::ProtocolEffects effects{};
        chat::runtime::EmitActionResultEffect action{};
        action.protocol = result.protocol;
        action.action = chat::runtime::ProtocolActionKind::TraceRoute;
        action.state = result.ok ? chat::runtime::ProtocolActionState::Delivered
                                 : chat::runtime::ProtocolActionState::Failed;
        action.peer = result.peer;
        action.request_id = result.request_id;
        effects.add(action);
        return effects;
    }

    chat::runtime::ProtocolEffects tick(
        const chat::runtime::RuntimeContext& context) override
    {
        ++tick_count;
        last_context = context;

        chat::runtime::ProtocolEffects effects{};
        chat::runtime::EmitActionResultEffect timed_out{};
        timed_out.protocol = context.protocol;
        timed_out.action = chat::runtime::ProtocolActionKind::ExchangePosition;
        timed_out.state = chat::runtime::ProtocolActionState::TimedOut;
        timed_out.peer = 0x7777UL;
        timed_out.request_id = 0x8888UL;
        effects.add(timed_out);
        return effects;
    }

    int prepare_count = 0;
    int incoming_count = 0;
    int tx_result_count = 0;
    int tick_count = 0;
    bool saw_trace_route = false;
    bool saw_send_text = false;
    bool saw_request_node_info = false;
    bool saw_share_position = false;
    chat::NodeId last_peer = 0;
    std::string last_text{};
    chat::MessageId next_request_id = 0xCAFEUL;
    chat::runtime::RuntimeContext last_context{};
    chat::runtime::TxResult last_tx{};
};

class RecordingExecutor final : public chat::runtime::IProtocolEffectExecutor
{
  public:
    bool execute(const chat::runtime::ProtocolEffect& effect) override
    {
        effects.push_back(effect);
        if (fail_send_text && std::get_if<chat::runtime::SendTextEffect>(&effect))
        {
            return false;
        }
        if (fail_send_packet && std::get_if<chat::runtime::SendPacketEffect>(&effect))
        {
            return false;
        }
        return true;
    }

    template <typename T>
    const T* effectAt(std::size_t index) const
    {
        if (index >= effects.size())
        {
            return nullptr;
        }
        return std::get_if<T>(&effects[index]);
    }

    bool fail_send_packet = false;
    bool fail_send_text = false;
    std::vector<chat::runtime::ProtocolEffect> effects{};
};

} // namespace

int main()
{
    static_assert(std::is_constructible<chat::runtime::MeshProtocolFacade,
                                        chat::runtime::IProtocolRuntime&,
                                        chat::runtime::IProtocolEffectExecutor&,
                                        const chat::runtime::IProtocolRuntimeContextProvider&>::value,
                  "MeshProtocolFacade must be a real injectable use-case boundary");

    StaticContextProvider context_provider{};
    context_provider.context.protocol = chat::MeshProtocol::Meshtastic;
    context_provider.context.self_node = 0xBEEFUL;
    context_provider.context.now_ms = 1234;

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result =
            facade.sendText(chat::ChannelId::SECONDARY, 0x11112222UL, "hello facade");

        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
        assert(result.failed_effect_count == 0);
        assert(runtime.prepare_count == 1);
        assert(runtime.saw_send_text);
        assert(runtime.last_peer == 0x11112222UL);
        assert(runtime.last_text == "hello facade");
        assert(executor.effects.size() == 1);

        const auto* text =
            executor.effectAt<chat::runtime::SendTextEffect>(0);
        assert(text);
        assert(text->protocol == chat::MeshProtocol::Meshtastic);
        assert(text->channel == chat::ChannelId::SECONDARY);
        assert(text->peer == 0x11112222UL);
        assert(text->message_id == runtime.next_request_id);
        assert(text->text == "hello facade");
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        executor.fail_send_text = true;
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result =
            facade.sendText(chat::ChannelId::PRIMARY, 0x33334444UL, "fail text");

        assert(!result.ok());
        assert(result.effect_count == 2);
        assert(result.executed_effect_count == 1);
        assert(result.failed_effect_count == 1);
        assert(result.hasActionResult());
        assert(runtime.tx_result_count == 1);
        assert(runtime.last_tx.request_id == runtime.next_request_id);
        assert(runtime.last_tx.peer == 0x33334444UL);
        assert(!runtime.last_tx.ok);
        assert(executor.effects.size() == 1);
        assert(executor.effectAt<chat::runtime::SendTextEffect>(0));
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result = facade.startTraceRoute(0x12345678UL);

        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
        assert(result.failed_effect_count == 0);
        assert(!result.hasActionResult());
        assert(runtime.prepare_count == 1);
        assert(runtime.saw_trace_route);
        assert(runtime.last_peer == 0x12345678UL);
        assert(runtime.last_context.self_node == context_provider.context.self_node);
        assert(executor.effects.size() == 1);

        const auto* packet =
            executor.effectAt<chat::runtime::SendPacketEffect>(0);
        assert(packet);
        assert(packet->protocol == chat::MeshProtocol::Meshtastic);
        assert(packet->dest == 0x12345678UL);
        assert(packet->request_id == runtime.next_request_id);
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        executor.fail_send_packet = true;
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result = facade.startTraceRoute(0x2222UL);

        assert(!result.ok());
        assert(result.effect_count == 2);
        assert(result.executed_effect_count == 1);
        assert(result.failed_effect_count == 1);
        assert(result.hasActionResult());
        assert(result.action_result.state == chat::runtime::ProtocolActionState::Failed);
        assert(result.action_result.peer == 0x2222UL);
        assert(result.action_result.request_id == runtime.next_request_id);
        assert(runtime.tx_result_count == 1);
        assert(runtime.last_tx.protocol == chat::MeshProtocol::Meshtastic);
        assert(runtime.last_tx.request_id == runtime.next_request_id);
        assert(runtime.last_tx.peer == 0x2222UL);
        assert(!runtime.last_tx.ok);
        assert(executor.effects.size() == 1);
        assert(executor.effectAt<chat::runtime::SendPacketEffect>(0));
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result = facade.requestNodeInfo(0x3333UL, false);

        assert(result.ok());
        assert(runtime.saw_request_node_info);
        assert(runtime.last_peer == 0x3333UL);
        assert(executor.effects.size() == 1);
        const auto* node_info =
            executor.effectAt<chat::runtime::SendNodeInfoEffect>(0);
        assert(node_info);
        assert(!node_info->want_response);
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        chat::runtime::SharePositionIntent intent{};
        intent.peer = 0x4444UL;
        intent.valid = true;
        const auto result = facade.sharePosition(intent);

        assert(result.ok());
        assert(runtime.saw_share_position);
        assert(runtime.last_peer == intent.peer);
        assert(executor.effectAt<chat::runtime::SendPacketEffect>(0));
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        chat::runtime::IncomingPacket packet{};
        packet.protocol = chat::MeshProtocol::Meshtastic;
        packet.from = 0x5555UL;
        packet.to = context_provider.context.self_node;
        packet.portnum = 99;
        packet.payload.assign({0xAA, 0xBB});

        const auto result = facade.handleIncoming(packet);

        assert(result.ok());
        assert(runtime.incoming_count == 1);
        assert(runtime.last_peer == packet.from);
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 0);
        assert(executor.effects.empty());
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(
            runtime,
            executor,
            context_provider,
            chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);

        chat::runtime::IncomingPacket packet{};
        packet.protocol = chat::MeshProtocol::Meshtastic;
        packet.from = 0x5656UL;
        packet.to = context_provider.context.self_node;
        packet.portnum = 100;
        packet.payload.assign({0xCC, 0xDD});

        const auto result = facade.handleIncoming(packet);

        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
        assert(executor.effects.size() == 1);
        assert(executor.effectAt<chat::runtime::PublishIncomingDataEffect>(0));
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        chat::runtime::TxResult tx{};
        tx.protocol = chat::MeshProtocol::Meshtastic;
        tx.peer = 0x6666UL;
        tx.request_id = 0x1212UL;
        tx.ok = true;

        const auto result = facade.handleTxResult(tx);

        assert(result.ok());
        assert(result.hasActionResult());
        assert(result.action_result.state == chat::runtime::ProtocolActionState::Delivered);
        assert(result.action_result.peer == tx.peer);
        assert(runtime.tx_result_count == 1);
        assert(result.executed_effect_count == 0);
        assert(executor.effects.empty());
    }

    {
        FakeRuntime runtime{};
        RecordingExecutor executor{};
        chat::runtime::MeshProtocolFacade facade(runtime, executor, context_provider);

        const auto result = facade.tick();

        assert(result.ok());
        assert(result.hasActionResult());
        assert(result.action_result.action ==
               chat::runtime::ProtocolActionKind::ExchangePosition);
        assert(result.action_result.state == chat::runtime::ProtocolActionState::TimedOut);
        assert(runtime.tick_count == 1);
        assert(result.executed_effect_count == 0);
        assert(executor.effects.empty());
    }

    return 0;
}
