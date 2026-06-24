#include "ui_chat_runtime/chat_delivery_feedback_controller.h"

namespace ui_chat_runtime
{

ChatDeliveryFeedbackController::ChatDeliveryFeedbackController(
    IChatDeliveryFeedbackPort& port)
    : port_(port)
{
}

void ChatDeliveryFeedbackController::onChatSendResult(
    chat::MessageId msg_id,
    bool success,
    const chat::ChatMessage* message)
{
    if (msg_id == 0)
    {
        return;
    }
    if (message == nullptr)
    {
        if (success)
        {
            return;
        }
    }
    else if (message->from != 0)
    {
        return;
    }
    if (alreadyNotified(msg_id, success))
    {
        return;
    }

    remember(msg_id, success);
    if (success)
    {
        port_.showChatDeliverySent(msg_id);
        return;
    }
    port_.showChatDeliveryFailed(msg_id);
}

void ChatDeliveryFeedbackController::clear()
{
    for (auto& item : recent_)
    {
        item = RecentResult{};
    }
    next_recent_ = 0;
}

bool ChatDeliveryFeedbackController::alreadyNotified(chat::MessageId msg_id,
                                                     bool success) const
{
    for (const auto& item : recent_)
    {
        if (item.msg_id == msg_id && item.success == success)
        {
            return true;
        }
    }
    return false;
}

void ChatDeliveryFeedbackController::remember(chat::MessageId msg_id,
                                              bool success)
{
    recent_[next_recent_] = RecentResult{msg_id, success};
    next_recent_ = (next_recent_ + 1) %
                   (sizeof(recent_) / sizeof(recent_[0]));
}

} // namespace ui_chat_runtime
