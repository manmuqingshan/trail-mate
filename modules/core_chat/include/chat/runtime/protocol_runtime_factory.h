#pragma once

#include "chat/runtime/mesh_protocol_facade.h"
#include "chat/runtime/protocol_runtime.h"

namespace chat::runtime
{

class FixedProtocolRuntimeContextProvider final : public IProtocolRuntimeContextProvider
{
  public:
    explicit FixedProtocolRuntimeContextProvider(RuntimeContext context)
        : context_(context)
    {
    }

    RuntimeContext runtimeContext() const override
    {
        return context_;
    }

    void setRuntimeContext(RuntimeContext context)
    {
        context_ = context;
    }

  private:
    RuntimeContext context_{};
};

struct ProtocolRuntimeSelection
{
    IProtocolRuntime* meshtastic = nullptr;
    IProtocolRuntime* meshcore = nullptr;
};

struct ProtocolRuntimeBundle
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    IProtocolRuntime* runtime = nullptr;
    IProtocolEffectExecutor* executor = nullptr;
    const IProtocolRuntimeContextProvider* context_provider = nullptr;

    bool valid() const
    {
        return runtime != nullptr && executor != nullptr && context_provider != nullptr;
    }

    MeshProtocolFacade createFacade(
        ProtocolProjectionPolicy projection_policy =
            ProtocolProjectionPolicy::CaptureAppFacing) const
    {
        return MeshProtocolFacade(*runtime, *executor, *context_provider, projection_policy);
    }
};

inline IProtocolRuntime* selectProtocolRuntime(MeshProtocol protocol,
                                               const ProtocolRuntimeSelection& selection)
{
    switch (protocol)
    {
    case MeshProtocol::Meshtastic:
        return selection.meshtastic;
    case MeshProtocol::MeshCore:
        return selection.meshcore;
    case MeshProtocol::RNode:
    case MeshProtocol::LXMF:
    default:
        return nullptr;
    }
}

inline ProtocolRuntimeBundle protocolRuntimeFor(
    MeshProtocol protocol,
    const ProtocolRuntimeSelection& selection,
    IProtocolEffectExecutor& executor,
    const IProtocolRuntimeContextProvider& context_provider)
{
    ProtocolRuntimeBundle bundle{};
    bundle.protocol = protocol;
    bundle.runtime = selectProtocolRuntime(protocol, selection);
    bundle.executor = &executor;
    bundle.context_provider = &context_provider;
    return bundle;
}

} // namespace chat::runtime
