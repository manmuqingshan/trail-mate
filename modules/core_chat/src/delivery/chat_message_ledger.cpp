#include "chat/delivery/chat_message_ledger.h"

#include "chat/delivery/chat_delivery_message_projection.h"
#include "chat/delivery/chat_delivery_send_result_projection.h"
#include "chat/delivery/chat_outbox_service.h"

namespace chat::delivery
{

ChatMessageLedger::ChatMessageLedger(ChatModel& model, IChatStore& store)
    : model_(model), store_(store)
{
}

void ChatMessageLedger::setDeliveryEventPort(
    IChatDeliveryEventPort* delivery_event_port)
{
    delivery_event_port_ = delivery_event_port;
}

void ChatMessageLedger::recordOutbound(const ChatMessage& message,
                                       bool model_enabled,
                                       SendFailureKind failure)
{
    if (model_enabled)
    {
        model_.onSendQueued(message);
        if (message.status == MessageStatus::Failed && message.msg_id != 0)
        {
            model_.onSendResult(message.msg_id, false);
        }
    }
    store_.append(message);
    if (ChatOutboxService::isOutboundStatusUpdate(message.status))
    {
        publishDeliveryEvent(message, message.status, 0, failure);
    }
}

bool ChatMessageLedger::applyOutboundStatus(MessageId msg_id,
                                            MessageStatus status,
                                            bool model_enabled,
                                            uint32_t timestamp_ms,
                                            SendFailureKind failure)
{
    ChatMessage current{};
    if (!lookupMessage(msg_id, current))
    {
        return false;
    }
    if (!ChatOutboxService::shouldApplyStatus(&current, status))
    {
        return false;
    }
    if (!writeStatus(msg_id, status, model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, status, timestamp_ms, failure);
    return true;
}

bool ChatMessageLedger::applyOutboundStatusForProtocol(MessageId msg_id,
                                                       MeshProtocol protocol,
                                                       MessageStatus status,
                                                       bool model_enabled,
                                                       uint32_t timestamp_ms,
                                                       SendFailureKind failure)
{
    ChatMessage current{};
    if (!lookupMessageForProtocol(msg_id, protocol, current))
    {
        return false;
    }
    if (!ChatOutboxService::shouldApplyStatus(&current, status))
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id, protocol, status, model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, status, timestamp_ms, failure);
    return true;
}

bool ChatMessageLedger::markRetryQueued(MessageId msg_id, bool model_enabled)
{
    ChatMessage current{};
    if (!lookupMessage(msg_id, current))
    {
        return false;
    }
    if (current.from != 0 || current.status != MessageStatus::Failed)
    {
        return false;
    }
    if (!writeStatus(msg_id, MessageStatus::Queued, model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, MessageStatus::Queued);
    return true;
}

bool ChatMessageLedger::markRetryQueuedForProtocol(MessageId msg_id,
                                                   MeshProtocol protocol,
                                                   bool model_enabled)
{
    ChatMessage current{};
    if (!lookupMessageForProtocol(msg_id, protocol, current))
    {
        return false;
    }
    if (current.from != 0 || current.status != MessageStatus::Failed)
    {
        return false;
    }
    if (!writeStatusForProtocol(msg_id,
                                protocol,
                                MessageStatus::Queued,
                                model_enabled))
    {
        return false;
    }
    publishDeliveryEvent(current, MessageStatus::Queued);
    return true;
}

bool ChatMessageLedger::lookupMessage(MessageId msg_id, ChatMessage& out) const
{
    if (msg_id == 0)
    {
        return false;
    }
    if (const ChatMessage* message = model_.getMessage(msg_id))
    {
        out = *message;
        return true;
    }
    return store_.getMessage(msg_id, &out);
}

bool ChatMessageLedger::lookupMessageForProtocol(MessageId msg_id,
                                                 MeshProtocol protocol,
                                                 ChatMessage& out) const
{
    if (msg_id == 0)
    {
        return false;
    }
    if (const ChatMessage* message =
            model_.getMessageForProtocol(msg_id, protocol))
    {
        out = *message;
        return true;
    }
    return store_.getMessageForProtocol(msg_id, protocol, &out);
}

bool ChatMessageLedger::writeStatus(MessageId msg_id,
                                    MessageStatus status,
                                    bool model_enabled)
{
    if (msg_id == 0)
    {
        return false;
    }
    bool updated = false;
    if (model_enabled)
    {
        updated = model_.updateMessageStatus(msg_id, status);
    }
    return store_.updateMessageStatus(msg_id, status) || updated;
}

bool ChatMessageLedger::writeStatusForProtocol(MessageId msg_id,
                                               MeshProtocol protocol,
                                               MessageStatus status,
                                               bool model_enabled)
{
    if (msg_id == 0)
    {
        return false;
    }
    bool updated = false;
    if (model_enabled)
    {
        updated =
            model_.updateMessageStatusForProtocol(msg_id, protocol, status);
    }
    return store_.updateMessageStatusForProtocol(msg_id, protocol, status) ||
           updated;
}

void ChatMessageLedger::publishDeliveryEvent(const ChatMessage& message,
                                             MessageStatus status,
                                             uint32_t timestamp_ms,
                                             SendFailureKind failure)
{
    if (delivery_event_port_ == nullptr || message.msg_id == 0 ||
        !ChatOutboxService::isOutboundStatusUpdate(status))
    {
        return;
    }

    delivery_event_port_->publishDeliveryEvent(
        makeChatSendResultDeliveryEvent(
            toDeliveryRef(message),
            ChatOutboxService::toDeliveryState(status),
            status == MessageStatus::Failed
                ? failure
                : ChatOutboxService::failureForStatus(status),
            timestamp_ms));
}

} // namespace chat::delivery
