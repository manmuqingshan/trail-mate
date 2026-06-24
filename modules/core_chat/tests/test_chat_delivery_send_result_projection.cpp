#include "chat/delivery/chat_delivery_send_result_projection.h"

#include <cassert>

int main()
{
    using namespace chat::delivery;

    ChatDeliveryRef ref{};
    ref.protocol_id = 10;

    ChatDeliveryEvent event = makeChatSendResultDeliveryEvent(
        ref,
        true,
        SendFailureKind::PeerKeyMissing,
        111);
    assert(event.ref.protocol_id == 10);
    assert(event.state == DeliveryState::Sent);
    assert(event.failure == SendFailureKind::None);
    assert(event.timestamp_ms == 111);

    ref.protocol_id = 11;
    event = makeChatSendResultDeliveryEvent(ref,
                                            false,
                                            SendFailureKind::None,
                                            222);
    assert(event.ref.protocol_id == 11);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::Unknown);
    assert(event.timestamp_ms == 222);

    event = makeChatSendResultDeliveryEvent(ref,
                                            false,
                                            SendFailureKind::RadioSendFailed,
                                            333);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::RadioSendFailed);
    assert(event.timestamp_ms == 333);

    event = makeAckTimeoutDeliveryEvent(ChatDeliveryRef{0, 12, 0}, 444);
    assert(event.ref.protocol_id == 12);
    assert(event.state == DeliveryState::Failed);
    assert(event.failure == SendFailureKind::AckTimeout);
    assert(event.timestamp_ms == 444);

    return 0;
}
