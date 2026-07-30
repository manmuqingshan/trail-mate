#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_send_result_projection.h"
#include "chat/delivery/chat_outbox_service.h"
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
    uint32_t timestamp_ms,
    ::chat::delivery::SendFailureKind failure,
    bool has_protocol,
    ::chat::MeshProtocol protocol)
{
    if (!::chat::delivery::ChatOutboxService::isOutboundStatusUpdate(status))
    {
        return;
    }

    const ::chat::ChatMessage* message = chat_service_.getMessage(msg_id);
    if (has_protocol)
    {
        message = chat_service_.getMessageForProtocol(msg_id, protocol);
    }
    if (!::chat::delivery::ChatOutboxService::shouldApplyStatus(message,
                                                                status))
    {
        return;
    }
    const auto state =
        ::chat::delivery::ChatOutboxService::toDeliveryState(status);
    if (status != ::chat::MessageStatus::Failed)
    {
        failure = ::chat::delivery::SendFailureKind::None;
    }
    (void)publishSendResult(msg_id,
                            has_protocol,
                            protocol,
                            state,
                            failure,
                            timestamp_ms);
}

void ChatDeliveryEventProjectionAdapter::onAckTimeout(
    ::chat::MessageId msg_id,
    uint32_t timestamp_ms)
{
    (void)publishSendResult(
        msg_id,
        false,
        ::chat::MeshProtocol::Meshtastic,
        ::chat::delivery::DeliveryState::Failed,
        ::chat::delivery::SendFailureKind::AckTimeout,
        timestamp_ms);
}

bool ChatDeliveryEventProjectionAdapter::publishSendResult(
    ::chat::MessageId msg_id,
    bool has_protocol,
    ::chat::MeshProtocol protocol,
    ::chat::delivery::DeliveryState state,
    ::chat::delivery::SendFailureKind failure,
    uint32_t timestamp_ms)
{
    if (msg_id == 0)
    {
        return false;
    }

    const ::chat::ChatMessage* message =
        has_protocol ? chat_service_.getMessageForProtocol(msg_id, protocol)
                     : chat_service_.getMessage(msg_id);
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
