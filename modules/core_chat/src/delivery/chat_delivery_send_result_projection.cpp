#include "chat/delivery/chat_delivery_send_result_projection.h"

namespace chat::delivery
{

ChatDeliveryEvent makeChatSendResultDeliveryEvent(ChatDeliveryRef ref,
                                                  bool success,
                                                  SendFailureKind failure,
                                                  uint32_t timestamp_ms)
{
    ChatDeliveryEvent event{};
    event.ref = ref;
    event.timestamp_ms = timestamp_ms;
    if (success)
    {
        event.state = DeliveryState::Sent;
        event.failure = SendFailureKind::None;
        return event;
    }

    event.state = DeliveryState::Failed;
    event.failure = failure == SendFailureKind::None ? SendFailureKind::Unknown
                                                     : failure;
    return event;
}

ChatDeliveryEvent makeAckTimeoutDeliveryEvent(ChatDeliveryRef ref,
                                              uint32_t timestamp_ms)
{
    return makeChatSendResultDeliveryEvent(ref,
                                           false,
                                           SendFailureKind::AckTimeout,
                                           timestamp_ms);
}

} // namespace chat::delivery
