#include "chat/delivery/chat_message_ledger.h"
#include "chat/infra/store/ram_store.h"

#include <cassert>

namespace
{

class RecordingDeliveryEventPort final
    : public chat::delivery::IChatDeliveryEventPort
{
  public:
    void publishDeliveryEvent(
        const chat::delivery::ChatDeliveryEvent& event) override
    {
        last = event;
        ++count;
    }

    chat::delivery::ChatDeliveryEvent last{};
    int count = 0;
};

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

chat::ChatMessage outgoing_with_protocol(chat::MessageId id,
                                         chat::MeshProtocol protocol,
                                         chat::NodeId peer)
{
    chat::ChatMessage message = outgoing(id, chat::MessageStatus::Queued);
    message.protocol = protocol;
    message.peer = peer;
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
    RecordingDeliveryEventPort delivery_events{};
    ledger.setDeliveryEventPort(&delivery_events);

    ledger.recordOutbound(outgoing(100, chat::MessageStatus::Queued), true);
    const chat::ChatMessage* model_message = model.getMessage(100);
    assert(model_message != nullptr);
    assert(model_message->status == chat::MessageStatus::Queued);
    assert(delivery_events.count == 1);
    assert(delivery_events.last.ref.protocol_id == 100);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Queued);

    chat::ChatMessage stored{};
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Queued);

    assert(ledger.applyOutboundStatus(100,
                                      chat::MessageStatus::Sent,
                                      true,
                                      1234));
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(store.getMessage(100, &stored));
    assert(stored.status == chat::MessageStatus::Sent);
    assert(delivery_events.count == 2);
    assert(delivery_events.last.ref.protocol_id == 100);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Sent);
    assert(delivery_events.last.timestamp_ms == 1234);

    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Queued, true));
    assert(delivery_events.count == 2);
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(delivery_events.count == 2);
    assert(model.getMessage(100)->status == chat::MessageStatus::Sent);

    assert(ledger.applyOutboundStatus(100,
                                      chat::MessageStatus::Delivered,
                                      true));
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);
    assert(delivery_events.count == 3);
    assert(delivery_events.last.state ==
           chat::delivery::DeliveryState::Delivered);
    assert(!ledger.applyOutboundStatus(100, chat::MessageStatus::Failed, true));
    assert(delivery_events.count == 3);
    assert(model.getMessage(100)->status == chat::MessageStatus::Delivered);

    ledger.recordOutbound(outgoing(200, chat::MessageStatus::Failed),
                          true,
                          chat::delivery::SendFailureKind::PeerKeyMissing);
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(delivery_events.count == 4);
    assert(delivery_events.last.ref.protocol_id == 200);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Failed);
    assert(delivery_events.last.failure ==
           chat::delivery::SendFailureKind::PeerKeyMissing);
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Queued, true));
    assert(!ledger.applyOutboundStatus(200, chat::MessageStatus::Sent, true));
    assert(delivery_events.count == 4);
    assert(model.getMessage(200)->status == chat::MessageStatus::Failed);
    assert(ledger.markRetryQueued(200, true));
    assert(model.getMessage(200)->status == chat::MessageStatus::Queued);
    assert(delivery_events.count == 5);
    assert(delivery_events.last.ref.protocol_id == 200);
    assert(delivery_events.last.ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(delivery_events.last.state == chat::delivery::DeliveryState::Queued);

    model.onIncoming(incoming(300));
    store.append(incoming(300));
    assert(!ledger.applyOutboundStatus(300, chat::MessageStatus::Sent, true));
    assert(!ledger.markRetryQueued(300, true));
    assert(delivery_events.count == 5);

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

    chat::ChatModel collision_model;
    chat::RamStore collision_store;
    chat::delivery::ChatMessageLedger collision_ledger(collision_model,
                                                       collision_store);
    collision_ledger.recordOutbound(
        outgoing_with_protocol(500,
                               chat::MeshProtocol::Meshtastic,
                               0x01020304),
        true);
    collision_ledger.recordOutbound(
        outgoing_with_protocol(500,
                               chat::MeshProtocol::Reticulum,
                               0x05060708),
        true);
    assert(collision_ledger.applyOutboundStatusForProtocol(
        500,
        chat::MeshProtocol::Reticulum,
        chat::MessageStatus::Delivered,
        true));
    assert(collision_ledger.applyOutboundStatusForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        chat::MessageStatus::Failed,
        true,
        2222,
        chat::delivery::SendFailureKind::AckTimeout));
    const chat::ChatMessage* meshtastic_collision =
        collision_model.getMessageForProtocol(500,
                                              chat::MeshProtocol::Meshtastic);
    const chat::ChatMessage* reticulum_collision =
        collision_model.getMessageForProtocol(500,
                                              chat::MeshProtocol::Reticulum);
    assert(meshtastic_collision != nullptr);
    assert(reticulum_collision != nullptr);
    assert(meshtastic_collision->status == chat::MessageStatus::Failed);
    assert(reticulum_collision->status == chat::MessageStatus::Delivered);
    assert(collision_ledger.markRetryQueuedForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        true));
    assert(meshtastic_collision->status == chat::MessageStatus::Queued);
    assert(collision_store.getMessageForProtocol(
        500,
        chat::MeshProtocol::Meshtastic,
        &stored));
    assert(stored.status == chat::MessageStatus::Queued);
    assert(collision_store.getMessageForProtocol(
        500,
        chat::MeshProtocol::Reticulum,
        &stored));
    assert(stored.status == chat::MessageStatus::Delivered);

    return 0;
}
