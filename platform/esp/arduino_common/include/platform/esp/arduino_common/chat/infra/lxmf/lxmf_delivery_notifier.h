/**
 * @file lxmf_delivery_notifier.h
 * @brief Reticulum/LXMF outbound delivery status publisher.
 */

#pragma once

#include "chat/domain/chat_types.h"

namespace chat::lxmf::runtime
{

class LxmfDeliveryNotifier
{
  public:
    LxmfDeliveryNotifier() = default;
    LxmfDeliveryNotifier(const LxmfDeliveryNotifier&) = delete;
    LxmfDeliveryNotifier& operator=(const LxmfDeliveryNotifier&) = delete;
    LxmfDeliveryNotifier(LxmfDeliveryNotifier&&) = delete;
    LxmfDeliveryNotifier& operator=(LxmfDeliveryNotifier&&) = delete;

    void publish(MessageId message_id, MessageStatus status) const;
    void publish(MessageId message_id, bool success) const;
    void queued(MessageId message_id) const;
    void sent(MessageId message_id) const;
    void delivered(MessageId message_id) const;
    void failed(MessageId message_id) const;
};

} // namespace chat::lxmf::runtime
