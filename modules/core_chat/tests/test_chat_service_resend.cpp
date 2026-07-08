#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>

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
        chat::MeshSendResult result =
            next_send_ok ? chat::MeshSendResult::success(id)
                         : chat::MeshSendResult::fail(next_failure, id);
        result.reticulum_identity = next_reticulum_identity;
        return result;
    }

    chat::MeshSendResult sendTextToReticulumDestination(
        chat::ChannelId channel,
        const std::string& text,
        chat::MessageId forced_msg_id,
        const chat::ReticulumPeerIdentity& destination) override
    {
        ++destination_send_count;
        last_channel = channel;
        last_text = text;
        last_forced_id = forced_msg_id;
        last_peer = 0;
        last_destination = destination;
        const chat::MessageId id =
            forced_msg_id != 0 ? forced_msg_id : next_msg_id++;
        chat::MeshSendResult result =
            next_send_ok ? chat::MeshSendResult::success(id)
                         : chat::MeshSendResult::fail(next_failure, id);
        result.reticulum_identity = destination;
        return result;
    }

    void pushIncoming(chat::NodeId from,
                      chat::MessageId msg_id,
                      const std::string& text,
                      const chat::ReticulumPeerIdentity& reticulum_identity =
                          chat::ReticulumPeerIdentity{},
                      chat::NodeId to = 0xFFFFFFFFUL)
    {
        chat::MeshIncomingText incoming{};
        incoming.channel = chat::ChannelId::PRIMARY;
        incoming.from = from;
        incoming.to = to;
        incoming.msg_id = msg_id;
        incoming.text = text;
        incoming.timestamp = 1;
        incoming.hop_limit = 3;
        incoming.encrypted = true;
        incoming.reticulum_identity = reticulum_identity;
        incoming_.push_back(incoming);
    }

    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        if (incoming_.empty())
        {
            return false;
        }
        if (out)
        {
            *out = incoming_.front();
        }
        incoming_.pop_front();
        return true;
    }

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
    chat::ReticulumPeerIdentity next_reticulum_identity{};
    int send_count = 0;
    int destination_send_count = 0;
    chat::ChannelId last_channel = chat::ChannelId::PRIMARY;
    std::string last_text;
    chat::MessageId last_forced_id = 0;
    chat::NodeId last_peer = 0;
    chat::ReticulumPeerIdentity last_destination{};

  private:
    std::deque<chat::MeshIncomingText> incoming_{};
};

class CountingIncomingObserver final : public chat::ChatService::IncomingMessageObserver
{
  public:
    void onIncomingMessage(const chat::ChatMessage& msg, const chat::RxMeta*) override
    {
        ++count;
        last_msg_id = msg.msg_id;
    }

    int count = 0;
    chat::MessageId last_msg_id = 0;
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

chat::ReticulumPeerIdentity makeReticulumIdentity(std::uint8_t destination_base,
                                                  std::uint8_t identity_base)
{
    std::uint8_t destination_hash[chat::kReticulumPeerHashSize] = {};
    std::uint8_t identity_hash[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] =
            static_cast<std::uint8_t>(destination_base + index);
        identity_hash[index] = static_cast<std::uint8_t>(identity_base + index);
    }
    return chat::makeReticulumPeerIdentity(destination_hash, identity_hash);
}

chat::ReticulumPeerIdentity makeReticulumDestination(std::uint8_t destination_base)
{
    std::uint8_t destination_hash[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] =
            static_cast<std::uint8_t>(destination_base + index);
    }
    return chat::makeReticulumDestinationIdentity(destination_hash);
}

void assertReticulumIdentityEquals(const chat::ReticulumPeerIdentity& actual,
                                   const chat::ReticulumPeerIdentity& expected)
{
    assert(actual.valid == expected.valid);
    assert(std::memcmp(actual.destination_hash,
                       expected.destination_hash,
                       chat::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(actual.identity_hash,
                       expected.identity_hash,
                       chat::kReticulumPeerHashSize) == 0);
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

    service.setActiveProtocol(chat::MeshProtocol::Reticulum);
    mesh.next_send_ok = true;
    mesh.next_msg_id = 99;
    mesh.next_reticulum_identity = makeReticulumIdentity(0x20, 0x60);
    const chat::MeshSendResult reticulum_send =
        service.sendTextDetailed(chat::ChannelId::PRIMARY, "reticulum hello", 0xAABBCCDD);
    assert(reticulum_send.ok);
    chat::ConversationId reticulum_conv(
        chat::ChannelId::PRIMARY,
        0xAABBCCDD,
        chat::MeshProtocol::Reticulum);
    reticulum_conv.reticulum_identity = mesh.next_reticulum_identity;
    msg = onlyMessage(service, reticulum_conv);
    assert(msg->msg_id == 99);
    assertReticulumIdentityEquals(msg->reticulum_identity,
                                  mesh.next_reticulum_identity);
    service.setActiveProtocol(chat::MeshProtocol::Meshtastic);
    mesh.next_reticulum_identity = {};

    {
        chat::ChatModel group_model;
        FakeMeshAdapter group_mesh;
        chat::RamStore group_store;
        chat::ChatService group_service(group_model,
                                        group_mesh,
                                        group_store,
                                        chat::MeshProtocol::Reticulum);
        const chat::ReticulumPeerIdentity group_identity =
            makeReticulumDestination(0x90);
        chat::ConversationId group_conv(chat::ChannelId::PRIMARY,
                                        0,
                                        chat::MeshProtocol::Reticulum);
        group_conv.reticulum_identity = group_identity;

        group_mesh.next_send_ok = true;
        group_mesh.next_msg_id = 700;
        const chat::MeshSendResult group_send =
            group_service.sendTextToConversationDetailed(group_conv, "group hello");
        assert(group_send.ok);
        assert(group_send.msg_id == 700);
        assert(group_mesh.send_count == 0);
        assert(group_mesh.destination_send_count == 1);
        assertReticulumIdentityEquals(group_mesh.last_destination, group_identity);

        const chat::ChatMessage* group_msg = onlyMessage(group_service, group_conv);
        assert(group_msg->peer == 0);
        assert(group_msg->status == chat::MessageStatus::Queued);
        assertReticulumIdentityEquals(group_msg->reticulum_identity, group_identity);
    }

    {
        chat::ChatModel group_model;
        FakeMeshAdapter group_mesh;
        chat::RamStore group_store;
        chat::ChatService group_service(group_model,
                                        group_mesh,
                                        group_store,
                                        chat::MeshProtocol::Reticulum);
        const chat::ReticulumPeerIdentity group_identity =
            makeReticulumDestination(0xA0);
        chat::ConversationId group_conv(chat::ChannelId::PRIMARY,
                                        0,
                                        chat::MeshProtocol::Reticulum);
        group_conv.reticulum_identity = group_identity;

        group_mesh.next_send_ok = false;
        group_mesh.next_msg_id = 800;
        const chat::MeshSendResult group_failed =
            group_service.sendTextToConversationDetailed(group_conv, "retry group");
        assert(!group_failed.ok);
        assert(group_failed.msg_id == 800);
        assert(group_mesh.destination_send_count == 1);

        group_mesh.next_send_ok = true;
        assert(group_service.resendFailed(800));
        assert(group_mesh.destination_send_count == 2);
        assert(group_mesh.last_forced_id == 800);
        assertReticulumIdentityEquals(group_mesh.last_destination, group_identity);
        const chat::ChatMessage* group_msg = onlyMessage(group_service, group_conv);
        assert(group_msg->status == chat::MessageStatus::Queued);
    }

    service.handleSendResult(42, true);
    msg = onlyMessage(service, conv);
    assert(msg->msg_id == 42);
    assert(msg->status == chat::MessageStatus::Sent);
    service.handleSendResult(42, false);
    msg = onlyMessage(service, conv);
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

    {
        chat::ChatModel incoming_model;
        FakeMeshAdapter incoming_mesh;
        chat::RamStore incoming_store;
        chat::ChatService incoming_service(incoming_model, incoming_mesh, incoming_store);
        CountingIncomingObserver incoming_observer;
        incoming_service.addIncomingMessageObserver(&incoming_observer);
        const chat::ConversationId broadcast(chat::ChannelId::PRIMARY,
                                             0,
                                             chat::MeshProtocol::Meshtastic);

        incoming_mesh.pushIncoming(0x1234ABCDU, 0x42U, "test");
        incoming_mesh.pushIncoming(0x1234ABCDU, 0x42U, "test");
        incoming_service.processIncoming();
        auto incoming_messages = incoming_store.loadRecent(broadcast, 10);
        assert(incoming_messages.size() == 1);
        assert(incoming_store.getUnread(broadcast) == 1);
        assert(incoming_observer.count == 1);

        for (std::uint32_t i = 0; i < 256U; ++i)
        {
            incoming_mesh.pushIncoming(0x1234ABCDU, 0x1000U + i, "window fill");
        }
        incoming_service.processIncoming();
        assert(incoming_observer.count == 257);

        incoming_mesh.pushIncoming(0x1234ABCDU, 0x10FFU, "recent duplicate");
        incoming_mesh.pushIncoming(0x1234ABCDU, 0x42U, "evicted original id");
        incoming_service.processIncoming();
        assert(incoming_observer.count == 258);
        assert(incoming_observer.last_msg_id == 0x42U);
    }

    {
        chat::ChatModel incoming_model;
        FakeMeshAdapter incoming_mesh;
        chat::RamStore incoming_store;
        chat::ChatService incoming_service(incoming_model, incoming_mesh, incoming_store);
        incoming_service.setActiveProtocol(chat::MeshProtocol::Reticulum);
        const chat::ReticulumPeerIdentity identity = makeReticulumIdentity(0x30, 0x70);
        chat::ConversationId reticulum_conv(chat::ChannelId::PRIMARY,
                                            0x1234ABCDU,
                                            chat::MeshProtocol::Reticulum);
        reticulum_conv.reticulum_identity = identity;

        incoming_mesh.pushIncoming(0x1234ABCDU,
                                   0x500U,
                                   "reticulum inbound",
                                   identity,
                                   incoming_mesh.getNodeId());
        incoming_mesh.pushIncoming(0x87654321U,
                                   0x500U,
                                   "same destination duplicate",
                                   identity,
                                   incoming_mesh.getNodeId());
        incoming_service.processIncoming();

        const auto incoming_messages = incoming_store.loadRecent(reticulum_conv, 10);
        assert(incoming_messages.size() == 1);
        assertReticulumIdentityEquals(incoming_messages.front().reticulum_identity,
                                      identity);

        size_t total = 0;
        const auto conversations = incoming_service.getConversations(0, 0, &total);
        assert(total == 1);
        assert(conversations.size() == 1);
        assert(conversations.front().id == reticulum_conv);
        assert(conversations.front().id.reticulum_identity.valid);
        assertReticulumIdentityEquals(conversations.front().reticulum_identity,
                                      identity);
    }

    return 0;
}
