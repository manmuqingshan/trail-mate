#include "chat/delivery/chat_delivery_send_result_projection.h"

namespace chat::delivery
{

ChatDeliveryEvent makeChatSendResultDeliveryEvent(ChatDeliveryRef ref,
                                                  bool success,
                                                  SendFailureKind failure,
                                                  uint32_t timestamp_ms)
{
    return makeChatSendResultDeliveryEvent(
        ref,
        success ? DeliveryState::Sent : DeliveryState::Failed,
        failure,
        timestamp_ms);
}

ChatDeliveryEvent makeChatSendResultDeliveryEvent(ChatDeliveryRef ref,
                                                  DeliveryState state,
                                                  SendFailureKind failure,
                                                  uint32_t timestamp_ms)
{
    ChatDeliveryEvent event{};
    event.ref = ref;
    event.timestamp_ms = timestamp_ms;
    if (state == DeliveryState::Queued || state == DeliveryState::Sending ||
        state == DeliveryState::Sent || state == DeliveryState::Delivered)
    {
        event.state = state;
        event.failure = SendFailureKind::None;
        return event;
    }

    event.state = state == DeliveryState::Failed ? DeliveryState::Failed
                                                 : DeliveryState::Unknown;
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
