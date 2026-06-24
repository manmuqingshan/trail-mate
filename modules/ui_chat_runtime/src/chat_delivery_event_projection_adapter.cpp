#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_send_result_projection.h"
#include "chat/usecase/chat_service.h"
#include "sys/event_bus.h"

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
    const ::sys::ChatSendResultEvent& event)
{
    const auto failure = event.success
                             ? ::chat::delivery::SendFailureKind::None
                             : ::chat::delivery::SendFailureKind::Unknown;
    (void)publishSendResult(event.msg_id,
                            event.success,
                            failure,
                            event.timestamp);
}

void ChatDeliveryEventProjectionAdapter::onAckTimeout(
    ::chat::MessageId msg_id,
    uint32_t timestamp_ms)
{
    (void)publishSendResult(
        msg_id,
        false,
        ::chat::delivery::SendFailureKind::AckTimeout,
        timestamp_ms);
}

bool ChatDeliveryEventProjectionAdapter::publishSendResult(
    ::chat::MessageId msg_id,
    bool success,
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
            success,
            failure,
            timestamp_ms);
    delivery_events_.publishDeliveryEvent(event);
    return true;
}

} // namespace ui_chat_runtime
