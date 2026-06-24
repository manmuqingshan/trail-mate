#pragma once

#include "chat/runtime/protocol_runtime.h"

#include <cstddef>
#include <string>

namespace chat::runtime
{

class IProtocolRuntimeContextProvider
{
  public:
    virtual ~IProtocolRuntimeContextProvider() = default;
    virtual RuntimeContext runtimeContext() const = 0;
};

struct MeshProtocolFacadeActionResult
{
    bool present = false;
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ProtocolActionKind action = ProtocolActionKind::Unknown;
    ProtocolActionState state = ProtocolActionState::Pending;
    NodeId peer = 0;
    MessageId request_id = 0;
    MessageId message_id = 0;
    int32_t detail = 0;
};

struct MeshProtocolFacadeResult
{
    std::size_t effect_count = 0;
    std::size_t executed_effect_count = 0;
    std::size_t failed_effect_count = 0;
    MeshProtocolFacadeActionResult action_result{};

    bool ok() const
    {
        return failed_effect_count == 0;
    }

    bool hasActionResult() const
    {
        return action_result.present;
    }
};

enum class ProtocolProjectionPolicy : uint8_t
{
    CaptureAppFacing = 0,
    ExecuteAppFacing,
};

class MeshProtocolFacade
{
  public:
    MeshProtocolFacade(IProtocolRuntime& runtime,
                       IProtocolEffectExecutor& executor,
                       const IProtocolRuntimeContextProvider& context_provider,
                       ProtocolProjectionPolicy projection_policy =
                           ProtocolProjectionPolicy::CaptureAppFacing)
        : runtime_(runtime),
          executor_(executor),
          context_provider_(context_provider),
          projection_policy_(projection_policy)
    {
    }

    MeshProtocolFacadeResult executeIntent(const ProtocolIntent& intent)
    {
        const RuntimeContext context = context_provider_.runtimeContext();
        return executeEffects(runtime_.prepareOutgoing(intent, context), context, true);
    }

    MeshProtocolFacadeResult startTraceRoute(NodeId peer,
                                             ChannelId channel = ChannelId::PRIMARY,
                                             uint32_t timeout_ms = 0)
    {
        TraceRouteIntent intent{};
        intent.channel = channel;
        intent.peer = peer;
        intent.timeout_ms = timeout_ms;
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult exchangePosition(NodeId peer,
                                              ChannelId channel = ChannelId::PRIMARY)
    {
        ExchangePositionIntent intent{};
        intent.channel = channel;
        intent.peer = peer;
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult requestNodeInfo(NodeId peer, bool want_response)
    {
        RequestNodeInfoIntent intent{};
        intent.peer = peer;
        intent.want_response = want_response;
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult sendText(ChannelId channel,
                                      NodeId peer,
                                      const std::string& text)
    {
        SendTextIntent intent{};
        intent.channel = channel;
        intent.peer = peer;
        intent.text = text;
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult sharePosition(const SharePositionIntent& intent)
    {
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult shareWaypoint(const ShareWaypointIntent& intent)
    {
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult discover(const DiscoverIntent& intent)
    {
        return executeIntent(intent);
    }

    MeshProtocolFacadeResult handleIncoming(const IncomingPacket& packet)
    {
        const RuntimeContext context = context_provider_.runtimeContext();
        return executeEffects(runtime_.handleIncomingPacket(packet, context).effects,
                              context,
                              true);
    }

    MeshProtocolFacadeResult handleTxResult(const TxResult& result)
    {
        const RuntimeContext context = context_provider_.runtimeContext();
        return executeEffects(runtime_.handleTxResult(result, context), context, false);
    }

    MeshProtocolFacadeResult tick()
    {
        const RuntimeContext context = context_provider_.runtimeContext();
        return executeEffects(runtime_.tick(context), context, true);
    }

  private:
    static void mergeResult(MeshProtocolFacadeResult& target,
                            const MeshProtocolFacadeResult& source)
    {
        target.effect_count += source.effect_count;
        target.executed_effect_count += source.executed_effect_count;
        target.failed_effect_count += source.failed_effect_count;
        if (source.action_result.present)
        {
            target.action_result = source.action_result;
        }
    }

    static void captureActionResult(MeshProtocolFacadeResult& result,
                                    const ProtocolEffect& effect)
    {
        if (const auto* action = std::get_if<EmitActionResultEffect>(&effect))
        {
            result.action_result.present = true;
            result.action_result.protocol = action->protocol;
            result.action_result.action = action->action;
            result.action_result.state = action->state;
            result.action_result.peer = action->peer;
            result.action_result.request_id = action->request_id;
            result.action_result.message_id = action->message_id;
            result.action_result.detail = action->detail;
        }
    }

    static bool isAppFacingProjection(const ProtocolEffect& effect)
    {
        return std::get_if<EmitActionResultEffect>(&effect) ||
               std::get_if<PublishIncomingTextEffect>(&effect) ||
               std::get_if<PublishIncomingDataEffect>(&effect) ||
               std::get_if<PublishNodeInfoEffect>(&effect);
    }

    bool shouldCaptureAppFacingProjection(const ProtocolEffect& effect) const
    {
        return projection_policy_ == ProtocolProjectionPolicy::CaptureAppFacing &&
               isAppFacingProjection(effect);
    }

    MeshProtocolFacadeResult executeEffects(const ProtocolEffects& effects,
                                            const RuntimeContext& context,
                                            bool allow_tx_feedback)
    {
        MeshProtocolFacadeResult result{};
        result.effect_count = effects.items.size();
        for (const ProtocolEffect& effect : effects.items)
        {
            captureActionResult(result, effect);
            if (shouldCaptureAppFacingProjection(effect))
            {
                continue;
            }
            const bool ok = executor_.execute(effect);
            ++result.executed_effect_count;
            if (ok)
            {
                continue;
            }

            ++result.failed_effect_count;
            if (!allow_tx_feedback)
            {
                continue;
            }

            TxResult tx{};
            if (const auto* text = std::get_if<SendTextEffect>(&effect))
            {
                if (text->message_id == 0)
                {
                    continue;
                }
                tx.protocol = text->protocol;
                tx.request_id = text->message_id;
                tx.peer = text->peer;
            }
            else if (const auto* packet = std::get_if<SendPacketEffect>(&effect))
            {
                if (packet->request_id == 0)
                {
                    continue;
                }
                tx.protocol = packet->protocol;
                tx.request_id = packet->request_id;
                tx.peer = packet->dest;
            }
            else
            {
                continue;
            }

            tx.ok = false;
            const MeshProtocolFacadeResult feedback =
                executeEffects(runtime_.handleTxResult(tx, context), context, false);
            mergeResult(result, feedback);
        }
        return result;
    }

    IProtocolRuntime& runtime_;
    IProtocolEffectExecutor& executor_;
    const IProtocolRuntimeContextProvider& context_provider_;
    ProtocolProjectionPolicy projection_policy_ = ProtocolProjectionPolicy::CaptureAppFacing;
};

} // namespace chat::runtime
