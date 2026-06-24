#pragma once

#include "chat/domain/chat_types.h"

#include <stddef.h>

namespace ui_chat_runtime
{

class IChatDeliveryFeedbackPort
{
  public:
    virtual ~IChatDeliveryFeedbackPort() = default;

    virtual void showChatDeliverySent(chat::MessageId msg_id) = 0;
    virtual void showChatDeliveryFailed(chat::MessageId msg_id) = 0;
};

class ChatDeliveryFeedbackController final
{
  public:
    explicit ChatDeliveryFeedbackController(IChatDeliveryFeedbackPort& port);

    void onChatSendResult(chat::MessageId msg_id,
                          bool success,
                          const chat::ChatMessage* message);
    void clear();

  private:
    struct RecentResult
    {
        chat::MessageId msg_id = 0;
        bool success = false;
    };

    bool alreadyNotified(chat::MessageId msg_id, bool success) const;
    void remember(chat::MessageId msg_id, bool success);

    IChatDeliveryFeedbackPort& port_;
    RecentResult recent_[8] = {};
    size_t next_recent_ = 0;
};

} // namespace ui_chat_runtime
