#include "chat/delivery/chat_outbox_service.h"

namespace chat::delivery
{

bool ChatOutboxService::isOutboundStatusUpdate(chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Queued:
    case chat::MessageStatus::Sent:
    case chat::MessageStatus::Delivered:
    case chat::MessageStatus::Failed:
        return true;
    case chat::MessageStatus::Incoming:
        return false;
    }
    return false;
}

bool ChatOutboxService::shouldApplyStatus(const chat::ChatMessage* current,
                                          chat::MessageStatus next)
{
    if (!isOutboundStatusUpdate(next))
    {
        return false;
    }
    if (current == nullptr)
    {
        return true;
    }
    if (current->from != 0)
    {
        return false;
    }
    if (current->status == next)
    {
        return true;
    }
    if (current->status == chat::MessageStatus::Delivered)
    {
        return false;
    }
    if (current->status == chat::MessageStatus::Sent &&
        (next == chat::MessageStatus::Queued ||
         next == chat::MessageStatus::Failed))
    {
        return false;
    }
    if (current->status == chat::MessageStatus::Failed &&
        (next == chat::MessageStatus::Queued ||
         next == chat::MessageStatus::Sent))
    {
        return false;
    }
    return true;
}

DeliveryState ChatOutboxService::toDeliveryState(chat::MessageStatus status)
{
    switch (status)
    {
    case chat::MessageStatus::Queued:
        return DeliveryState::Queued;
    case chat::MessageStatus::Sent:
        return DeliveryState::Sent;
    case chat::MessageStatus::Delivered:
        return DeliveryState::Delivered;
    case chat::MessageStatus::Failed:
        return DeliveryState::Failed;
    case chat::MessageStatus::Incoming:
        return DeliveryState::Received;
    }
    return DeliveryState::Unknown;
}

SendFailureKind ChatOutboxService::failureForStatus(
    chat::MessageStatus status)
{
    return status == chat::MessageStatus::Failed ? SendFailureKind::Unknown
                                                 : SendFailureKind::None;
}

} // namespace chat::delivery
