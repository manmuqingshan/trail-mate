#include "chat_presentation_adapters/chat_message_mapper.h"

#include <cassert>

namespace
{

void messageStatusesMapToDeliveryStates()
{
    assert(chat_presentation_adapters::mapMessageStatus(chat::MessageStatus::Incoming) ==
           ui::chat::MessageDeliveryState::Received);
    assert(chat_presentation_adapters::mapMessageStatus(chat::MessageStatus::Queued) ==
           ui::chat::MessageDeliveryState::Queued);
    assert(chat_presentation_adapters::mapMessageStatus(chat::MessageStatus::Sent) ==
           ui::chat::MessageDeliveryState::Sent);
    assert(chat_presentation_adapters::mapMessageStatus(chat::MessageStatus::Failed) ==
           ui::chat::MessageDeliveryState::Failed);
}

void messageStatusesMapToFailureKinds()
{
    assert(chat_presentation_adapters::mapMessageFailure(chat::MessageStatus::Incoming) ==
           ui::chat::MessageFailureKind::None);
    assert(chat_presentation_adapters::mapMessageFailure(chat::MessageStatus::Queued) ==
           ui::chat::MessageFailureKind::None);
    assert(chat_presentation_adapters::mapMessageFailure(chat::MessageStatus::Sent) ==
           ui::chat::MessageFailureKind::None);
    assert(chat_presentation_adapters::mapMessageFailure(chat::MessageStatus::Failed) ==
           ui::chat::MessageFailureKind::Unknown);
}

void rxOriginsMapToIngressTransport()
{
    assert(chat_presentation_adapters::mapMessageIngressTransport(chat::RxOrigin::Unknown) ==
           ui::chat::MessageIngressTransport::Unknown);
    assert(chat_presentation_adapters::mapMessageIngressTransport(chat::RxOrigin::Mesh) ==
           ui::chat::MessageIngressTransport::LoRa);
    assert(chat_presentation_adapters::mapMessageIngressTransport(chat::RxOrigin::LoRa) ==
           ui::chat::MessageIngressTransport::LoRa);
    assert(chat_presentation_adapters::mapMessageIngressTransport(chat::RxOrigin::External) ==
           ui::chat::MessageIngressTransport::Mqtt);
    assert(chat_presentation_adapters::mapMessageIngressTransport(chat::RxOrigin::WiFi) ==
           ui::chat::MessageIngressTransport::WiFi);
}

void incomingMessageMapsToRemoteStoredRef()
{
    chat::ChatMessage message;
    message.status = chat::MessageStatus::Incoming;
    message.from = 1234;
    message.msg_id = 77;

    const ui::chat::MessageRef ref =
        chat_presentation_adapters::toUiMessageRef(message);

    assert(ref.origin == ui::chat::MessageOrigin::RemoteStored);
    assert(ref.protocol_id == 77);
    assert(ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(ref.local_id == 0);
    assert(ref.nonce_or_seq == 0);
    assert(ref.isValid());
}

void queuedMessageMapsToLocalPendingRef()
{
    chat::ChatMessage message;
    message.status = chat::MessageStatus::Queued;
    message.from = 0;
    message.msg_id = 88;

    const ui::chat::MessageRef ref =
        chat_presentation_adapters::toUiMessageRef(message);

    assert(ref.origin == ui::chat::MessageOrigin::LocalPending);
    assert(ref.protocol_id == 88);
    assert(ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(ref.isValid());
}

void storedLocalMessageMapsToLocalStoredRef()
{
    chat::ChatMessage message;
    message.status = chat::MessageStatus::Sent;
    message.from = 0;
    message.msg_id = 99;

    const ui::chat::MessageRef ref =
        chat_presentation_adapters::toUiMessageRef(message);

    assert(ref.origin == ui::chat::MessageOrigin::LocalStored);
    assert(ref.protocol_id == 99);
    assert(ref.protocol ==
           static_cast<uint8_t>(chat::MeshProtocol::Meshtastic));
    assert(ref.isValid());
}

} // namespace

int main()
{
    messageStatusesMapToDeliveryStates();
    messageStatusesMapToFailureKinds();
    rxOriginsMapToIngressTransport();
    incomingMessageMapsToRemoteStoredRef();
    queuedMessageMapsToLocalPendingRef();
    storedLocalMessageMapsToLocalStoredRef();
    return 0;
}
