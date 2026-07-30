#pragma once

#include "chat/delivery/chat_delivery_event_projector.h"
#include "chat/domain/chat_types.h"

namespace chat::delivery
{

class ChatOutboxService final
{
  public:
    static bool isOutboundStatusUpdate(chat::MessageStatus status);
    static bool shouldApplyStatus(const chat::ChatMessage* current,
                                  chat::MessageStatus next);
    static DeliveryState toDeliveryState(chat::MessageStatus status);
    static SendFailureKind failureForStatus(chat::MessageStatus status);
};

} // namespace chat::delivery
