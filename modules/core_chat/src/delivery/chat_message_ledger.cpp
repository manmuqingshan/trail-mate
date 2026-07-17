#include "chat/delivery/chat_message_ledger.h"

#include "chat/delivery/chat_outbox_service.h"

namespace chat::delivery
{

ChatMessageLedger::ChatMessageLedger(ChatModel& model, IChatStore& store)
    : model_(model), store_(store)
{
}

void ChatMessageLedger::recordOutbound(const ChatMessage& message,
                                       bool model_enabled)
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
}

bool ChatMessageLedger::applyOutboundStatus(MessageId msg_id,
                                            MessageStatus status,
                                            bool model_enabled)
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
    return writeStatus(msg_id, status, model_enabled);
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
    return writeStatus(msg_id, MessageStatus::Queued, model_enabled);
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

} // namespace chat::delivery
