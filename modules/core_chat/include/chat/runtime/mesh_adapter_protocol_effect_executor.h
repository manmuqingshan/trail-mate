#pragma once

#include "chat/ports/i_mesh_adapter.h"
#include "chat/runtime/protocol_runtime.h"

namespace chat::runtime
{

enum class ProtocolEffectExecutionState : uint8_t
{
    NoSupportedEffect,
    Sent,
    Failed,
};

struct ProtocolEffectExecutionResult
{
    ProtocolEffectExecutionState state = ProtocolEffectExecutionState::NoSupportedEffect;
    MessageId request_id = 0;

    bool sent() const
    {
        return state == ProtocolEffectExecutionState::Sent;
    }
};

class MeshAdapterProtocolEffectExecutor final : public IProtocolEffectExecutor
{
  public:
    explicit MeshAdapterProtocolEffectExecutor(IMeshAdapter& adapter)
        : adapter_(adapter)
    {
    }

    bool execute(const ProtocolEffect& effect) override
    {
        if (const auto* text = std::get_if<SendTextEffect>(&effect))
        {
            return sendText(*text).sent();
        }
        if (const auto* packet = std::get_if<SendPacketEffect>(&effect))
        {
            return sendPacket(*packet).sent();
        }
        return false;
    }

    ProtocolEffectExecutionResult sendText(const SendTextEffect& text)
    {
        const MeshSendResult sent =
            adapter_.sendTextDetailed(text.channel, text.text, text.message_id, text.peer);
        ProtocolEffectExecutionResult result{};
        result.state = sent.ok ? ProtocolEffectExecutionState::Sent
                               : ProtocolEffectExecutionState::Failed;
        result.request_id = sent.msg_id != 0 ? sent.msg_id : text.message_id;
        return result;
    }

    ProtocolEffectExecutionResult sendPacket(const SendPacketEffect& packet)
    {
        if (packet.response_request_id != 0)
        {
            ProtocolEffectExecutionResult result{};
            result.state = ProtocolEffectExecutionState::Failed;
            result.request_id = packet.response_request_id;
            return result;
        }

        const uint8_t* payload = packet.payload.empty() ? nullptr : packet.payload.data();
        const bool ok = adapter_.sendAppData(packet.channel,
                                             packet.portnum,
                                             payload,
                                             packet.payload.size(),
                                             packet.dest,
                                             packet.want_ack,
                                             packet.request_id,
                                             packet.want_response);
        ProtocolEffectExecutionResult result{};
        result.state = ok ? ProtocolEffectExecutionState::Sent
                          : ProtocolEffectExecutionState::Failed;
        result.request_id = packet.request_id;
        return result;
    }

    static ProtocolEffectExecutionResult executeFirstSendPacket(
        IMeshAdapter& adapter,
        const ProtocolEffects& effects)
    {
        MeshAdapterProtocolEffectExecutor executor(adapter);
        for (const auto& effect : effects.items)
        {
            if (const auto* packet = std::get_if<SendPacketEffect>(&effect))
            {
                return executor.sendPacket(*packet);
            }
        }
        return ProtocolEffectExecutionResult{};
    }

  private:
    IMeshAdapter& adapter_;
};

} // namespace chat::runtime
