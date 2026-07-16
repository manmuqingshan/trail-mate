#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_send_result_projection.h"
#include "chat/usecase/chat_service.h"

namespace ui_chat_runtime
{

ChatDeliveryEventProjectionAdapter::ChatDeliveryEventProjectionAdapter(
    ::chat::ChatService& chat_service,
    ::chat::delivery::IChatDeliveryEventPort& delivery_events)
    : chat_service_(chat_service),
      delivery_events_(delivery_events)
{
}

void ChatDeliveryEventProjectionAdapter::onChatSendResult(
    ::chat::MessageId msg_id,
    ::chat::MessageStatus status,
    uint32_t timestamp_ms)
{
    if (status != ::chat::MessageStatus::Sent &&
        status != ::chat::MessageStatus::Delivered &&
        status != ::chat::MessageStatus::Failed)
    {
        return;
    }

    const ::chat::ChatMessage* message = chat_service_.getMessage(msg_id);
    if (status == ::chat::MessageStatus::Failed && message != nullptr &&
        (message->status == ::chat::MessageStatus::Sent ||
         message->status == ::chat::MessageStatus::Delivered))
    {
        return;
    }
    const auto state = status == ::chat::MessageStatus::Delivered
                           ? ::chat::delivery::DeliveryState::Delivered
                       : status == ::chat::MessageStatus::Sent
                           ? ::chat::delivery::DeliveryState::Sent
                           : ::chat::delivery::DeliveryState::Failed;
    const auto failure = status != ::chat::MessageStatus::Failed
                             ? ::chat::delivery::SendFailureKind::None
                             : ::chat::delivery::SendFailureKind::Unknown;
    (void)publishSendResult(msg_id, state, failure, timestamp_ms);
}

void ChatDeliveryEventProjectionAdapter::onAckTimeout(
    ::chat::MessageId msg_id,
    uint32_t timestamp_ms)
{
    (void)publishSendResult(
        msg_id,
        ::chat::delivery::DeliveryState::Failed,
        ::chat::delivery::SendFailureKind::AckTimeout,
        timestamp_ms);
}

bool ChatDeliveryEventProjectionAdapter::publishSendResult(
    ::chat::MessageId msg_id,
    ::chat::delivery::DeliveryState state,
    ::chat::delivery::SendFailureKind failure,
    uint32_t timestamp_ms)
{
    if (msg_id == 0)
    {
        return false;
    }

    const ::chat::ChatMessage* message = chat_service_.getMessage(msg_id);
    if (message == nullptr)
    {
        return false;
    }

    ::chat::delivery::ChatDeliveryEvent event =
        ::chat::delivery::makeChatSendResultDeliveryEvent(
            ::chat::delivery::toDeliveryRef(*message),
            state,
            failure,
            timestamp_ms);
    delivery_events_.publishDeliveryEvent(event);
    return true;
}

} // namespace ui_chat_runtime
