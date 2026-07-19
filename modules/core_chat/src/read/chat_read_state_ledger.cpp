#include "chat/read/chat_read_state_ledger.h"

namespace chat::read
{

ChatReadStateLedger::ChatReadStateLedger(ChatModel& model, IChatStore& store)
    : model_(model), store_(store)
{
}

bool ChatReadStateLedger::markRead(const ConversationId& conversation,
                                   bool model_enabled)
{
    if (!store_.setUnread(conversation, 0))
    {
        return false;
    }
    if (model_enabled)
    {
        model_.markRead(conversation);
    }
    return true;
}

int ChatReadStateLedger::unread(const ConversationId& conversation) const
{
    return store_.getUnread(conversation);
}

} // namespace chat::read
