#include "chat/runtime/protocol_runtime_factory.h"

#include <cassert>

namespace
{

class FakeRuntime final : public chat::runtime::IProtocolRuntime
{
  public:
    void prepareOutgoing(
        const chat::runtime::ProtocolIntent&,
        const chat::runtime::RuntimeContext&,
        chat::runtime::ProtocolEffects& effects) override
    {
        ++prepare_count;
        chat::runtime::SendTextEffect text{};
        text.protocol = protocol;
        text.text = "factory";
        effects.add(text);
    }

    void handleIncoming(
        const chat::runtime::IncomingPacket&,
        const chat::runtime::RuntimeContext&,
        chat::runtime::ProtocolEffects& effects) override
    {
        chat::runtime::PublishIncomingDataEffect data{};
        effects.add(data);
    }

    void handleTxResult(
        const chat::runtime::TxResult&,
        const chat::runtime::RuntimeContext&,
        chat::runtime::ProtocolTxFeedbackEffects&) override
    {
    }

    void tick(const chat::runtime::RuntimeContext&,
              chat::runtime::ProtocolEffects&) override
    {
    }

    chat::MeshProtocol protocol = chat::MeshProtocol::Meshtastic;
    int prepare_count = 0;
};

class RecordingExecutor final : public chat::runtime::IProtocolEffectExecutor
{
  public:
    bool execute(const chat::runtime::ProtocolEffect& effect) override
    {
        ++execute_count;
        if (const auto* text = std::get_if<chat::runtime::SendTextEffect>(&effect))
        {
            last_protocol = text->protocol;
        }
        return true;
    }

    chat::MeshProtocol last_protocol = chat::MeshProtocol::Meshtastic;
    int execute_count = 0;
};

} // namespace

int main()
{
    FakeRuntime meshtastic{};
    meshtastic.protocol = chat::MeshProtocol::Meshtastic;
    FakeRuntime meshcore{};
    meshcore.protocol = chat::MeshProtocol::MeshCore;
    FakeRuntime reticulum{};
    reticulum.protocol = chat::MeshProtocol::Reticulum;

    chat::runtime::ProtocolRuntimeSelection selection{};
    selection.meshtastic = &meshtastic;
    selection.meshcore = &meshcore;
    selection.reticulum = &reticulum;

    chat::runtime::RuntimeContext context{};
    context.protocol = chat::MeshProtocol::MeshCore;
    context.self_node = 0x1234UL;
    chat::runtime::FixedProtocolRuntimeContextProvider context_provider(context);

    RecordingExecutor executor{};
    chat::runtime::ProtocolEffectWorkspace workspace{};

    {
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              selection,
                                                              executor,
                                                              context_provider);
        assert(bundle.valid());
        assert(bundle.runtime == &meshtastic);
        assert(bundle.executor == &executor);
        assert(bundle.context_provider == &context_provider);
    }

    {
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::MeshCore,
                                                              selection,
                                                              executor,
                                                              context_provider);
        assert(bundle.valid());
        assert(bundle.runtime == &meshcore);
        auto facade = bundle.createFacade(workspace);
        const auto result =
            facade.sendText(chat::ChannelId::PRIMARY, 0x99UL, "through factory");
        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
        assert(meshcore.prepare_count == 1);
        assert(executor.execute_count == 1);
        assert(executor.last_protocol == chat::MeshProtocol::MeshCore);
    }

    {
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::MeshCore,
                                                              selection,
                                                              executor,
                                                              context_provider);
        auto facade = bundle.createFacade(
            workspace,
            chat::runtime::ProtocolProjectionPolicy::ExecuteAppFacing);
        chat::runtime::IncomingPacket packet{};
        packet.protocol = chat::MeshProtocol::MeshCore;
        const auto result = facade.handleIncoming(packet);
        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
    }

    {
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::RNode,
                                                              selection,
                                                              executor,
                                                              context_provider);
        assert(bundle.valid());
        assert(bundle.protocol == chat::MeshProtocol::Reticulum);
        assert(bundle.runtime == &reticulum);
    }

    {
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Reticulum,
                                                              selection,
                                                              executor,
                                                              context_provider);
        assert(bundle.valid());
        assert(bundle.protocol == chat::MeshProtocol::Reticulum);
        assert(bundle.runtime == &reticulum);
        auto facade = bundle.createFacade(workspace);
        const auto result =
            facade.sendText(chat::ChannelId::PRIMARY, 0x44UL, "through reticulum factory");
        assert(result.ok());
        assert(result.effect_count == 1);
        assert(result.executed_effect_count == 1);
        assert(reticulum.prepare_count == 1);
        assert(executor.last_protocol == chat::MeshProtocol::Reticulum);
    }

    {
        context.protocol = chat::MeshProtocol::Meshtastic;
        context.self_node = 0xCAFEUL;
        context_provider.setRuntimeContext(context);
        assert(context_provider.runtimeContext().protocol == chat::MeshProtocol::Meshtastic);
        assert(context_provider.runtimeContext().self_node == 0xCAFEUL);
    }

    return 0;
}
