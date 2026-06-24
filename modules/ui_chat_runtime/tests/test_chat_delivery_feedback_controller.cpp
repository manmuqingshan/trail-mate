#include "ui_chat_runtime/chat_delivery_feedback_controller.h"

#include <cassert>

namespace
{

class FakeFeedbackPort final
    : public ::ui_chat_runtime::IChatDeliveryFeedbackPort
{
  public:
    void showChatDeliverySent(chat::MessageId msg_id) override
    {
        ++sent_count;
        last_id = msg_id;
    }

    void showChatDeliveryFailed(chat::MessageId msg_id) override
    {
        ++failed_count;
        last_id = msg_id;
    }

    int sent_count = 0;
    int failed_count = 0;
    chat::MessageId last_id = 0;
};

chat::ChatMessage outgoing(chat::MessageId id)
{
    chat::ChatMessage message;
    message.msg_id = id;
    message.from = 0;
    return message;
}

chat::ChatMessage incoming(chat::MessageId id)
{
    chat::ChatMessage message = outgoing(id);
    message.from = 0x12345678;
    return message;
}

} // namespace

int main()
{
    FakeFeedbackPort port;
    ::ui_chat_runtime::ChatDeliveryFeedbackController controller(port);

    chat::ChatMessage sent = outgoing(100);
    controller.onChatSendResult(100, true, &sent);
    assert(port.sent_count == 1);
    assert(port.failed_count == 0);
    assert(port.last_id == 100);

    controller.onChatSendResult(100, true, &sent);
    assert(port.sent_count == 1);

    chat::ChatMessage failed = outgoing(101);
    controller.onChatSendResult(101, false, &failed);
    assert(port.sent_count == 1);
    assert(port.failed_count == 1);
    assert(port.last_id == 101);

    chat::ChatMessage rx = incoming(102);
    controller.onChatSendResult(102, true, &rx);
    controller.onChatSendResult(103, false, nullptr);
    controller.onChatSendResult(0, false, &failed);
    assert(port.sent_count == 1);
    assert(port.failed_count == 2);
    assert(port.last_id == 103);

    controller.onChatSendResult(103, false, nullptr);
    assert(port.failed_count == 2);

    controller.onChatSendResult(104, true, nullptr);
    assert(port.sent_count == 1);

    controller.clear();
    controller.onChatSendResult(100, true, &sent);
    assert(port.sent_count == 2);
    assert(port.last_id == 100);

    return 0;
}
