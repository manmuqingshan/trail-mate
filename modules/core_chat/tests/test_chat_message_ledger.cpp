#include "chat/delivery/chat_message_ledger.h"
#include "chat/infra/store/ram_store.h"

#include <cassert>

namespace
{

chat::ChatMessage outgoing(chat::MessageId id, chat::MessageStatus status)
{
    chat::ChatMessage message;
    message.protocol = chat::MeshProtocol::Meshtastic;
    message.channel = chat::ChannelId::PRIMARY;
    message.from = 0;
    message.peer = 0xAABBCCDD;
    message.msg_id = id;
    message.text = "ledger";
    message.status = status;
    return message;
}

chat::ChatMessage incoming(chat::MessageId id)
{
    chat::ChatMessage message = outgoing(id, chat::MessageStatus::Incoming);
    message.from = 0x11223344;
    return message;
}

} // namespace

int main()
{
    chat::ChatModel model;
    chat::RamStore store;
    chat::delivery::ChatMessageLedger ledger(model, store);

    ledger.recordOutbound(outgoing(100, chat::MessageStatus::Queued), true);
    const chat::ChatMessage* model_message = model.getMessage(100);
    assert(model_message != nullptr);
    assert(model_message->status == chat::MessageStatus::Queued);

    chat::ChatMessage stored{};
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Queued);

    assert(ledger.applyOutboundStatus(100, chat::MessageStatus::Sent, true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Sent);

    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Queued, true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);

    assert(ledger.applyOutboundStatus(100,
                                      chat::MessageStatus::Delivered,
                                      true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);

    ledger.recordOutbound(outgoing(200, chat::MessageStatus::Failed), true);
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Queued, true));
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Sent, true));
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(ledger.markRetryQueued(200, true));
    assert(model.getMessage(200)->status == chat::MessageStatus::Queued);

    model.onIncoming(incoming(300));
    store.append(incoming(300));
    assert(!ledger.applyOutboundStatus(300, chat::MessageStatus::Sent, true));
    assert(!ledger.markRetryQueued(300, true));

    chat::ChatModel store_only_model;
    chat::RamStore store_only_store;
    chat::delivery::ChatMessageLedger store_only_ledger(store_only_model,
                                                        store_only_store);
    store_only_ledger.recordOutbound(
        outgoing(400, chat::MessageStatus::Queued),
        false);
    assert(store_only_model.getMessage(400) == nullptr);
    assert(store_only_ledger.applyOutboundStatus(400,
                                                 chat::MessageStatus::Sent,
                                                 false));
    assert(store_only_store.getMessage(400, &stored));
    assert(stored.status == chat::MessageStatus::Sent);

    return 0;
}
