#include "chat/delivery/chat_outbox_service.h"

#include <cassert>

namespace
{

chat::ChatMessage outgoing(chat::MessageStatus status)
{
    chat::ChatMessage message;
    message.from = 0;
    message.status = status;
    return message;
}

chat::ChatMessage incoming()
{
    chat::ChatMessage message;
    message.from = 1234;
    message.status = chat::MessageStatus::Incoming;
    return message;
}

} // namespace

int main()
{
    using chat::MessageStatus;
    using chat::delivery::ChatOutboxService;
    using chat::delivery::DeliveryState;

    assert(ChatOutboxService::isOutboundStatusUpdate(MessageStatus::Queued));
    assert(ChatOutboxService::isOutboundStatusUpdate(MessageStatus::Sent));
    assert(ChatOutboxService::isOutboundStatusUpdate(
        MessageStatus::Delivered));
    assert(ChatOutboxService::isOutboundStatusUpdate(MessageStatus::Failed));
    assert(!ChatOutboxService::isOutboundStatusUpdate(
        MessageStatus::Incoming));

    chat::ChatMessage queued = outgoing(MessageStatus::Queued);
    assert(ChatOutboxService::shouldApplyStatus(&queued,
                                                MessageStatus::Queued));
    assert(ChatOutboxService::shouldApplyStatus(&queued, MessageStatus::Sent));
    assert(ChatOutboxService::shouldApplyStatus(&queued,
                                                MessageStatus::Delivered));
    assert(ChatOutboxService::shouldApplyStatus(&queued,
                                                MessageStatus::Failed));

    chat::ChatMessage sent = outgoing(MessageStatus::Sent);
    assert(ChatOutboxService::shouldApplyStatus(&sent, MessageStatus::Sent));
    assert(!ChatOutboxService::shouldApplyStatus(&sent,
                                                 MessageStatus::Queued));
    assert(!ChatOutboxService::shouldApplyStatus(&sent,
                                                 MessageStatus::Failed));
    assert(ChatOutboxService::shouldApplyStatus(&sent,
                                                MessageStatus::Delivered));

    chat::ChatMessage failed = outgoing(MessageStatus::Failed);
    assert(ChatOutboxService::shouldApplyStatus(&failed,
                                                MessageStatus::Failed));
    assert(!ChatOutboxService::shouldApplyStatus(&failed,
                                                 MessageStatus::Queued));
    assert(!ChatOutboxService::shouldApplyStatus(&failed,
                                                 MessageStatus::Sent));
    assert(ChatOutboxService::shouldApplyStatus(&failed,
                                                MessageStatus::Delivered));

    chat::ChatMessage delivered = outgoing(MessageStatus::Delivered);
    assert(ChatOutboxService::shouldApplyStatus(&delivered,
                                                MessageStatus::Delivered));
    assert(!ChatOutboxService::shouldApplyStatus(&delivered,
                                                 MessageStatus::Queued));
    assert(!ChatOutboxService::shouldApplyStatus(&delivered,
                                                 MessageStatus::Sent));
    assert(!ChatOutboxService::shouldApplyStatus(&delivered,
                                                 MessageStatus::Failed));

    chat::ChatMessage rx = incoming();
    assert(!ChatOutboxService::shouldApplyStatus(&rx, MessageStatus::Sent));

    assert(ChatOutboxService::toDeliveryState(MessageStatus::Queued) ==
           DeliveryState::Queued);
    assert(ChatOutboxService::toDeliveryState(MessageStatus::Sent) ==
           DeliveryState::Sent);
    assert(ChatOutboxService::toDeliveryState(MessageStatus::Delivered) ==
           DeliveryState::Delivered);
    assert(ChatOutboxService::toDeliveryState(MessageStatus::Failed) ==
           DeliveryState::Failed);

    return 0;
}
