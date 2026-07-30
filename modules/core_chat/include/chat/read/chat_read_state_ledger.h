#pragma once

#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"

namespace chat::read
{

class ChatReadStateLedger final
{
  public:
    ChatReadStateLedger(ChatModel& model, IChatStore& store);

    bool markRead(const ConversationId& conversation, bool model_enabled);
    int unread(const ConversationId& conversation) const;

  private:
    ChatModel& model_;
    IChatStore& store_;
};

} // namespace chat::read
