#pragma once

#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/domain/chat_types.h"

namespace chat
{
class ChatService;
}

namespace ui_chat_runtime
{

class ChatDeliveryEventProjectionAdapter
{
  public:
    ChatDeliveryEventProjectionAdapter(
        ::chat::ChatService& chat_service,
        ::chat::delivery::IChatDeliveryEventPort& delivery_events);

    void onChatSendResult(::chat::MessageId msg_id,
                          ::chat::MessageStatus status,
                          uint32_t timestamp_ms = 0);
    void onAckTimeout(::chat::MessageId msg_id, uint32_t timestamp_ms = 0);

  private:
    bool publishSendResult(::chat::MessageId msg_id,
                           ::chat::delivery::DeliveryState state,
                           ::chat::delivery::SendFailureKind failure,
                           uint32_t timestamp_ms);

    ::chat::ChatService& chat_service_;
    ::chat::delivery::IChatDeliveryEventPort& delivery_events_;
};

} // namespace ui_chat_runtime
