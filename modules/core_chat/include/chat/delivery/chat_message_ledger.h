#pragma once

#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"

namespace chat::delivery
{

class ChatMessageLedger final
{
  public:
    ChatMessageLedger(ChatModel& model, IChatStore& store);

    void setDeliveryEventPort(IChatDeliveryEventPort* delivery_event_port);

    void recordOutbound(const ChatMessage& message,
                        bool model_enabled,
                        SendFailureKind failure = SendFailureKind::Unknown);
    bool applyOutboundStatus(MessageId msg_id,
                             MessageStatus status,
                             bool model_enabled,
                             uint32_t timestamp_ms = 0,
                             SendFailureKind failure =
                                 SendFailureKind::Unknown);
    bool applyOutboundStatusForProtocol(MessageId msg_id,
                                        MeshProtocol protocol,
                                        MessageStatus status,
                                        bool model_enabled,
                                        uint32_t timestamp_ms = 0,
                                        SendFailureKind failure =
                                            SendFailureKind::Unknown);
    bool markRetryQueued(MessageId msg_id, bool model_enabled);
    bool markRetryQueuedForProtocol(MessageId msg_id,
                                    MeshProtocol protocol,
                                    bool model_enabled);

  private:
    bool lookupMessage(MessageId msg_id, ChatMessage& out) const;
    bool lookupMessageForProtocol(MessageId msg_id,
                                  MeshProtocol protocol,
                                  ChatMessage& out) const;
    bool writeStatus(MessageId msg_id,
                     MessageStatus status,
                     bool model_enabled);
    bool writeStatusForProtocol(MessageId msg_id,
                                MeshProtocol protocol,
                                MessageStatus status,
                                bool model_enabled);
    void publishDeliveryEvent(const ChatMessage& message,
                              MessageStatus status,
                              uint32_t timestamp_ms = 0,
                              SendFailureKind failure =
                                  SendFailureKind::Unknown);

    ChatModel& model_;
    IChatStore& store_;
    IChatDeliveryEventPort* delivery_event_port_ = nullptr;
};

} // namespace chat::delivery
