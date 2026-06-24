#include "chat/runtime/mesh_adapter_protocol_effect_executor.h"
#include "chat/runtime/meshtastic_runtime.h"
#include "chat/runtime/protocol_runtime.h"

#include <cassert>
#include <string>
#include <type_traits>
#include <vector>

namespace
{

class RecordingProtocolExecutor final : public chat::runtime::IProtocolEffectExecutor
{
  public:
    bool execute(const chat::runtime::ProtocolEffect& effect) override
    {
        effects.push_back(effect);
        return next_result;
    }

    template <typename T>
    const T* effectAt(size_t index) const
    {
        if (index >= effects.size())
        {
            return nullptr;
        }
        return std::get_if<T>(&effects[index]);
    }

    bool next_result = true;
    std::vector<chat::runtime::ProtocolEffect> effects{};
};

class FakeMeshAdapter final : public chat::IMeshAdapter
{
  public:
    bool sendText(chat::ChannelId,
                  const std::string&,
                  chat::MessageId* out_msg_id,
                  chat::NodeId = 0) override
    {
        if (out_msg_id)
        {
            *out_msg_id = 1;
        }
        return true;
    }

    bool sendTextWithId(chat::ChannelId channel,
                        const std::string& text,
                        chat::MessageId forced_msg_id,
                        chat::MessageId* out_msg_id,
                        chat::NodeId peer = 0) override
    {
        last_text_channel = channel;
        last_text = text;
        last_text_peer = peer;
        last_text_forced_msg_id = forced_msg_id;
        if (out_msg_id)
        {
            *out_msg_id = forced_msg_id != 0 ? forced_msg_id : 1;
        }
        return next_text_result;
    }

    bool pollIncomingText(chat::MeshIncomingText*) override
    {
        return false;
    }

    bool sendAppData(chat::ChannelId channel,
                     uint32_t portnum,
                     const uint8_t* payload,
                     size_t len,
                     chat::NodeId dest = 0,
                     bool want_ack = false,
                     chat::MessageId packet_id = 0,
                     bool want_response = false) override
    {
        last_channel = channel;
        last_portnum = portnum;
        last_dest = dest;
        last_want_ack = want_ack;
        last_packet_id = packet_id;
        last_want_response = want_response;
        last_payload.clear();
        if (payload != nullptr && len > 0)
        {
            last_payload.assign(payload, payload + len);
        }
        return next_send_result;
    }

    bool pollIncomingData(chat::MeshIncomingData*) override
    {
        return false;
    }

    void applyConfig(const chat::MeshConfig&) override {}

    bool isReady() const override
    {
        return true;
    }

    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override
    {
        return false;
    }

    bool next_send_result = true;
    bool next_text_result = true;
    chat::ChannelId last_text_channel = chat::ChannelId::PRIMARY;
    std::string last_text{};
    chat::NodeId last_text_peer = 0;
    chat::MessageId last_text_forced_msg_id = 0;
    chat::ChannelId last_channel = chat::ChannelId::PRIMARY;
    uint32_t last_portnum = 0;
    chat::NodeId last_dest = 0;
    bool last_want_ack = false;
    chat::MessageId last_packet_id = 0;
    bool last_want_response = false;
    std::vector<uint8_t> last_payload{};
};

bool executeAll(chat::runtime::IProtocolEffectExecutor& executor,
                const chat::runtime::ProtocolEffects& effects)
{
    bool ok = true;
    for (const auto& effect : effects.items)
    {
        ok = executor.execute(effect) && ok;
    }
    return ok;
}

} // namespace

int main()
{
    static_assert(std::is_base_of<chat::runtime::IProtocolEffectExecutor,
                                  RecordingProtocolExecutor>::value,
                  "RecordingProtocolExecutor must implement the effect bridge");

    {
        chat::runtime::SendDiscoverRequestEffect effect{};
        effect.protocol = chat::MeshProtocol::MeshCore;
        effect.tag = 0xAABBCCDDUL;
        effect.type_filter = 0x7FU;
        effect.prefix_only = true;
        effect.since = 1781258000UL;
        effect.rx_guard_ms = 9000;

        chat::runtime::ProtocolEffects effects{};
        effects.add(effect);
        RecordingProtocolExecutor executor{};
        assert(executeAll(executor, effects));
        assert(executor.effects.size() == 1);

        const auto* discover =
            executor.effectAt<chat::runtime::SendDiscoverRequestEffect>(0);
        assert(discover);
        assert(discover->protocol == chat::MeshProtocol::MeshCore);
        assert(discover->tag == effect.tag);
        assert(discover->type_filter == effect.type_filter);
        assert(discover->prefix_only == effect.prefix_only);
        assert(discover->since == effect.since);
        assert(discover->rx_guard_ms == effect.rx_guard_ms);
    }

    {
        chat::runtime::MeshtasticRuntime runtime{};
        chat::runtime::MeshtasticPkiResyncInput input{};
        input.cause = chat::runtime::MeshtasticPkiResyncCause::PeerKeyStale;
        input.peer = 0x12345678UL;
        input.channel = chat::ChannelId::SECONDARY;
        input.request_id = 0xCAFEUL;

        RecordingProtocolExecutor executor{};
        assert(executeAll(executor, runtime.handlePkiResync(input)));
        assert(executor.effects.size() == 3);

        const auto* forget =
            executor.effectAt<chat::runtime::ForgetPeerKeyEffect>(0);
        assert(forget);
        assert(forget->protocol == chat::MeshProtocol::Meshtastic);
        assert(forget->peer == input.peer);

        const auto* node_info =
            executor.effectAt<chat::runtime::SendNodeInfoEffect>(1);
        assert(node_info);
        assert(node_info->protocol == chat::MeshProtocol::Meshtastic);
        assert(node_info->channel == input.channel);
        assert(node_info->peer == input.peer);
        assert(node_info->want_response);

        const auto* routing_error =
            executor.effectAt<chat::runtime::SendRoutingErrorEffect>(2);
        assert(routing_error);
        assert(routing_error->protocol == chat::MeshProtocol::Meshtastic);
        assert(routing_error->channel == input.channel);
        assert(routing_error->peer == input.peer);
        assert(routing_error->request_id == input.request_id);
    }

    {
        RecordingProtocolExecutor executor{};
        executor.next_result = false;

        chat::runtime::SendSelfAnnouncementEffect effect{};
        effect.protocol = chat::MeshProtocol::MeshCore;

        chat::runtime::ProtocolEffects effects{};
        effects.add(effect);

        assert(!executeAll(executor, effects));
        assert(executor.effects.size() == 1);
        assert(executor.effectAt<chat::runtime::SendSelfAnnouncementEffect>(0));
    }

    {
        chat::runtime::SendTextEffect text{};
        text.protocol = chat::MeshProtocol::Meshtastic;
        text.channel = chat::ChannelId::SECONDARY;
        text.peer = 0x55667788UL;
        text.message_id = 0x11223344UL;
        text.text = "hello bridge";

        FakeMeshAdapter adapter{};
        chat::runtime::MeshAdapterProtocolEffectExecutor executor(adapter);

        assert(executor.execute(text));
        assert(adapter.last_text_channel == text.channel);
        assert(adapter.last_text == text.text);
        assert(adapter.last_text_peer == text.peer);
        assert(adapter.last_text_forced_msg_id == text.message_id);
    }

    {
        chat::runtime::SendTextEffect text{};
        text.message_id = 0x99UL;
        text.text = "fail";

        FakeMeshAdapter adapter{};
        adapter.next_text_result = false;
        chat::runtime::MeshAdapterProtocolEffectExecutor executor(adapter);

        assert(!executor.execute(text));
        assert(adapter.last_text_forced_msg_id == text.message_id);
    }

    {
        chat::runtime::SendPacketEffect packet{};
        packet.channel = chat::ChannelId::SECONDARY;
        packet.portnum = 0x1234U;
        packet.dest = 0x11223344UL;
        packet.want_ack = true;
        packet.request_id = 0xAABBCCDDUL;
        packet.want_response = true;
        packet.payload.assign({1, 2, 3, 4});

        chat::runtime::ProtocolEffects effects{};
        effects.add(packet);

        FakeMeshAdapter adapter{};
        const auto result =
            chat::runtime::MeshAdapterProtocolEffectExecutor::executeFirstSendPacket(
                adapter,
                effects);
        assert(result.state == chat::runtime::ProtocolEffectExecutionState::Sent);
        assert(result.request_id == packet.request_id);
        assert(adapter.last_channel == packet.channel);
        assert(adapter.last_portnum == packet.portnum);
        assert(adapter.last_dest == packet.dest);
        assert(adapter.last_want_ack == packet.want_ack);
        assert(adapter.last_packet_id == packet.request_id);
        assert(adapter.last_want_response == packet.want_response);
        assert(adapter.last_payload == packet.payload);
    }

    {
        FakeMeshAdapter adapter{};
        adapter.next_send_result = false;

        chat::runtime::SendPacketEffect packet{};
        packet.request_id = 0x1010UL;

        chat::runtime::ProtocolEffects effects{};
        effects.add(packet);

        const auto result =
            chat::runtime::MeshAdapterProtocolEffectExecutor::executeFirstSendPacket(
                adapter,
                effects);
        assert(result.state == chat::runtime::ProtocolEffectExecutionState::Failed);
        assert(result.request_id == packet.request_id);
    }

    {
        chat::runtime::ProtocolEffects effects{};
        chat::runtime::EmitActionResultEffect failed{};
        effects.add(failed);

        FakeMeshAdapter adapter{};
        const auto result =
            chat::runtime::MeshAdapterProtocolEffectExecutor::executeFirstSendPacket(
                adapter,
                effects);
        assert(result.state ==
               chat::runtime::ProtocolEffectExecutionState::NoSupportedEffect);
        assert(result.request_id == 0);
    }

    return 0;
}
