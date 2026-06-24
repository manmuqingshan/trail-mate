#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"

#include <cassert>

namespace
{

class FakeMeshAdapter final : public chat::IMeshAdapter
{
  public:
    bool sendText(chat::ChannelId channel,
                  const std::string& text,
                  chat::MessageId* out_msg_id,
                  chat::NodeId peer = 0) override
    {
        const chat::MeshSendResult result =
            sendTextDetailed(channel, text, 0, peer);
        if (out_msg_id)
        {
            *out_msg_id = result.msg_id;
        }
        return result.ok;
    }

    chat::MeshSendResult sendTextDetailed(chat::ChannelId channel,
                                          const std::string& text,
                                          chat::MessageId forced_msg_id = 0,
                                          chat::NodeId peer = 0) override
    {
        ++send_count;
        last_channel = channel;
        last_text = text;
        last_forced_id = forced_msg_id;
        last_peer = peer;
        const chat::MessageId id =
            forced_msg_id != 0 ? forced_msg_id : next_msg_id++;
        if (next_send_ok)
        {
            return chat::MeshSendResult::success(id);
        }
        return chat::MeshSendResult::fail(next_failure, id);
    }

    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }

    bool sendAppData(chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     chat::NodeId = 0,
                     bool = false,
                     chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }

    bool pollIncomingData(chat::MeshIncomingData*) override { return false; }
    void applyConfig(const chat::MeshConfig&) override {}
    chat::NodeId getNodeId() const override { return 0x01020304; }
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t& out_len, size_t) override
    {
        out_len = 0;
        return false;
    }

    bool next_send_ok = false;
    chat::MeshOperationFailure next_failure = chat::MeshOperationFailure::Unknown;
    chat::MessageId next_msg_id = 100;
    int send_count = 0;
    chat::ChannelId last_channel = chat::ChannelId::PRIMARY;
    std::string last_text;
    chat::MessageId last_forced_id = 0;
    chat::NodeId last_peer = 0;
};

const chat::ChatMessage* onlyMessage(chat::ChatService& service,
                                     const chat::ConversationId& conv)
{
    static chat::ChatMessage cache;
    const auto list = service.getRecentMessages(conv, 10);
    assert(list.size() == 1);
    cache = list.front();
    return &cache;
}

} // namespace

int main()
{
    chat::ChatModel model;
    FakeMeshAdapter mesh;
    chat::RamStore store;
    chat::ChatService service(model, mesh, store);

    const chat::ConversationId meshcore_conv(
        chat::ChannelId::PRIMARY,
        0x11223344,
        chat::MeshProtocol::MeshCore);
    const chat::ConversationId meshtastic_conv(
        chat::ChannelId::PRIMARY,
        0x11223344,
        chat::MeshProtocol::Meshtastic);
    assert(!service.canSendToConversation(meshcore_conv));
    assert(service.canSendToConversation(meshtastic_conv));
    assert(!service.sendTextToConversationDetailed(meshcore_conv, "wrong proto").ok);
    assert(mesh.send_count == 0);

    mesh.next_send_ok = false;
    mesh.next_msg_id = 42;
    const chat::MeshSendResult failed =
        service.sendTextDetailed(chat::ChannelId::PRIMARY, "hello", 0xAABBCCDD);
    assert(!failed.ok);
    assert(failed.msg_id == 42);

    const chat::ConversationId conv(
        chat::ChannelId::PRIMARY,
        0xAABBCCDD,
        chat::MeshProtocol::Meshtastic);
    const chat::ChatMessage* msg = onlyMessage(service, conv);
    assert(msg->msg_id == 42);
    assert(msg->status == chat::MessageStatus::Failed);
    assert(model.getFailedMessages().size() == 1);

    mesh.next_send_ok = true;
    assert(service.resendFailed(42));
    assert(mesh.last_forced_id == 42);
    assert(mesh.last_peer == 0xAABBCCDD);
    assert(mesh.last_text == "hello");
    msg = onlyMessage(service, conv);
    assert(msg->msg_id == 42);
    assert(msg->status == chat::MessageStatus::Queued);
    assert(service.getMessage(42)->status == chat::MessageStatus::Queued);
    assert(model.getFailedMessages().empty());

    assert(!service.resendFailed(42));

    service.handleSendResult(42, true);
    msg = onlyMessage(service, conv);
    assert(msg->msg_id == 42);
    assert(msg->status == chat::MessageStatus::Sent);

    mesh.next_send_ok = false;
    mesh.next_msg_id = 77;
    const chat::MeshSendResult second_failed =
        service.sendTextDetailed(chat::ChannelId::PRIMARY, "again", 0xAABBCCDD);
    assert(!second_failed.ok);
    assert(second_failed.msg_id == 77);
    assert(service.resendFailed(77) == false);
    const auto list = service.getRecentMessages(conv, 10);
    assert(list.size() == 2);
    assert(list.back().msg_id == 77);
    assert(list.back().status == chat::MessageStatus::Failed);

    mesh.next_send_ok = false;
    mesh.next_msg_id = 88;
    const chat::MeshSendResult third_failed =
        service.sendTextToConversationDetailed(conv, "proto retry guard");
    assert(!third_failed.ok);
    assert(third_failed.msg_id == 88);
    const int before_cross_protocol_retry = mesh.send_count;
    service.setActiveProtocol(chat::MeshProtocol::MeshCore);
    assert(!service.resendFailed(88));
    assert(mesh.send_count == before_cross_protocol_retry);

    return 0;
}
