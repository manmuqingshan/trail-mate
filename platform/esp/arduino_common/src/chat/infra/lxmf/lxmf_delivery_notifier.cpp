/**
 * @file lxmf_delivery_notifier.cpp
 * @brief Reticulum/LXMF outbound delivery status publisher.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_notifier.h"

#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
#include "sys/event_bus.h"
#endif

namespace chat::lxmf::runtime
{

void LxmfDeliveryNotifier::publish(MessageId message_id,
                                   MessageStatus status) const
{
    if (message_id == 0)
    {
        return;
    }
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    sys::EventBus::publish(
        new sys::ChatSendResultEvent(message_id,
                                     status,
                                     MeshProtocol::Reticulum),
        0);
#else
    (void)status;
#endif
}

void LxmfDeliveryNotifier::publish(MessageId message_id, bool success) const
{
    publish(message_id,
            success ? MessageStatus::Sent : MessageStatus::Failed);
}

void LxmfDeliveryNotifier::queued(MessageId message_id) const
{
    publish(message_id, MessageStatus::Queued);
}

void LxmfDeliveryNotifier::sent(MessageId message_id) const
{
    publish(message_id, MessageStatus::Sent);
}

void LxmfDeliveryNotifier::delivered(MessageId message_id) const
{
    publish(message_id, MessageStatus::Delivered);
}

void LxmfDeliveryNotifier::failed(MessageId message_id) const
{
    publish(message_id, MessageStatus::Failed);
}

} // namespace chat::lxmf::runtime
