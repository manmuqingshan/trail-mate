#include "chat/infra/store/ram_store.h"
#include "chat/read/chat_read_state_ledger.h"

#include <cassert>

namespace
{

chat::ConversationId conversation(chat::MeshProtocol protocol,
                                  chat::ChannelId channel,
                                  chat::NodeId peer)
{
    return chat::ConversationId(channel, peer, protocol);
}

chat::ChatMessage incoming(chat::MessageId id,
                           const chat::ConversationId& conv)
{
    chat::ChatMessage message;
    message.protocol = conv.protocol;
    message.channel = conv.channel;
    message.from = conv.peer == 0 ? 0x11223344 : conv.peer;
    message.peer = conv.peer;
    message.msg_id = id;
    message.timestamp = 1000 + id;
    message.text = "read";
    message.status = chat::MessageStatus::Incoming;
    return message;
}

class FailingUnreadStore final : public chat::RamStore
{
  public:
    bool setUnread(const chat::ConversationId& conv, int unread) override
    {
        (void)conv;
        (void)unread;
        return false;
    }
};

} // namespace

int main()
{
    const chat::ConversationId mt_direct =
        conversation(chat::MeshProtocol::Meshtastic,
                     chat::ChannelId::PRIMARY,
                     0xAABBCCDD);

    chat::ChatModel model;
    chat::RamStore store;
    model.onIncoming(incoming(1, mt_direct));
    store.append(incoming(1, mt_direct));
    assert(model.getUnread(mt_direct) == 1);
    assert(store.getUnread(mt_direct) == 1);

    chat::read::ChatReadStateLedger ledger(model, store);
    assert(ledger.markRead(mt_direct, true));
    assert(model.getUnread(mt_direct) == 0);
    assert(store.getUnread(mt_direct) == 0);

    chat::ChatModel failing_model;
    FailingUnreadStore failing_store;
    failing_model.onIncoming(incoming(2, mt_direct));
    failing_store.append(incoming(2, mt_direct));
    chat::read::ChatReadStateLedger failing_ledger(failing_model,
                                                   failing_store);
    assert(!failing_ledger.markRead(mt_direct, true));
    assert(failing_model.getUnread(mt_direct) == 1);
    assert(failing_store.getUnread(mt_direct) == 1);

    const chat::ConversationId reticulum_broadcast =
        conversation(chat::MeshProtocol::Reticulum,
                     chat::ChannelId::PRIMARY,
                     0);
    chat::RamStore protocol_store;
    chat::ChatModel protocol_model;
    protocol_store.append(incoming(7, reticulum_broadcast));
    protocol_model.onIncoming(incoming(7, reticulum_broadcast));
    chat::read::ChatReadStateLedger protocol_ledger(protocol_model,
                                                    protocol_store);
    assert(protocol_ledger.unread(reticulum_broadcast) == 1);
    assert(protocol_ledger.markRead(reticulum_broadcast, true));
    assert(protocol_ledger.unread(reticulum_broadcast) == 0);

    return 0;
}
