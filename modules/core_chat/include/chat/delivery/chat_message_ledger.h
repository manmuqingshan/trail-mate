#pragma once

#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"

namespace chat::delivery
{

class ChatMessageLedger final
{
  public:
    ChatMessageLedger(ChatModel& model, IChatStore& store);

    void recordOutbound(const ChatMessage& message, bool model_enabled);
    bool applyOutboundStatus(MessageId msg_id,
                             MessageStatus status,
                             bool model_enabled);
    bool markRetryQueued(MessageId msg_id, bool model_enabled);

  private:
    bool lookupMessage(MessageId msg_id, ChatMessage& out) const;
    bool writeStatus(MessageId msg_id,
                     MessageStatus status,
                     bool model_enabled);

    ChatModel& model_;
    IChatStore& store_;
};

} // namespace chat::delivery
