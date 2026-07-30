#pragma once

#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::delivery
{

enum class LedgerPersistence : uint8_t
{
    Durable,
    Deferred,
    Rejected,
};

class ChatMessageLedger final
{
  public:
    ChatMessageLedger(ChatModel& model, IChatStore& store);

    void setDeliveryEventPort(IChatDeliveryEventPort* delivery_event_port);

    LedgerPersistence recordOutbound(
        const ChatMessage& message,
        bool model_enabled,
        SendFailureKind failure = SendFailureKind::Unknown);
    LedgerPersistence recordIncoming(const ChatMessage& message);
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
    std::size_t flushPendingWrites(std::size_t budget = 1);
    std::vector<ChatMessage> loadPageFromLatest(
        const ConversationId& conversation,
        std::size_t offset_from_latest,
        std::size_t limit,
        std::size_t* total) const;
    std::vector<ChatMessage> loadRecent(const ConversationId& conversation,
                                        std::size_t limit) const;
    std::vector<ConversationMeta> loadConversationPage(MeshProtocol protocol,
                                                       std::size_t offset,
                                                       std::size_t limit,
                                                       std::size_t* total) const;
    bool findMessage(MessageId msg_id, ChatMessage& out) const;
    bool findMessageForProtocol(MessageId msg_id,
                                MeshProtocol protocol,
                                ChatMessage& out) const;
    void clearConversation(const ConversationId& conversation);
    void clear();

  private:
    static constexpr std::size_t kPendingOutboundWriteDepth = 8;
    static constexpr std::size_t kPendingStatusWriteDepth = 16;
    static constexpr std::size_t kTrackedOutboundDepth = 16;

    struct PendingOutboundWrite
    {
        bool used = false;
        uint32_t sequence = 0;
        ChatMessage message{};
    };

    struct PendingStatusWrite
    {
        bool used = false;
        uint32_t sequence = 0;
        MessageId msg_id = 0;
        MeshProtocol protocol = MeshProtocol::Meshtastic;
        MessageStatus status = MessageStatus::Queued;
    };

    struct TrackedOutbound
    {
        bool used = false;
        uint32_t sequence = 0;
        ConversationId conversation{};
        MessageId msg_id = 0;
        MeshProtocol protocol = MeshProtocol::Meshtastic;
        MessageStatus status = MessageStatus::Queued;
    };

    bool lookupMessage(MessageId msg_id, ChatMessage& out) const;
    bool lookupMessageForProtocol(MessageId msg_id,
                                  MeshProtocol protocol,
                                  ChatMessage& out) const;
    bool writeStatusForProtocol(MessageId msg_id,
                                MeshProtocol protocol,
                                MessageStatus status,
                                bool model_enabled);
    PendingOutboundWrite* findPendingOutbound(MessageId msg_id);
    PendingOutboundWrite* findPendingOutboundForProtocol(
        MessageId msg_id,
        MeshProtocol protocol);
    const PendingOutboundWrite* findPendingOutbound(MessageId msg_id) const;
    const PendingOutboundWrite* findPendingOutboundForProtocol(
        MessageId msg_id,
        MeshProtocol protocol) const;
    bool enqueuePendingOutbound(const ChatMessage& message);
    bool enqueuePendingStatus(MessageId msg_id,
                              MeshProtocol protocol,
                              MessageStatus status);
    void trackOutbound(const ChatMessage& message);
    TrackedOutbound* findTrackedOutbound(MessageId msg_id);
    TrackedOutbound* findTrackedOutboundForProtocol(
        MessageId msg_id,
        MeshProtocol protocol);
    void clearTrackedOutbound(const ConversationId& conversation);
    void removePendingStatus(MessageId msg_id, MeshProtocol protocol);
    void applyPendingStatus(ChatMessage& message) const;
    int oldestPendingOutboundIndex() const;
    int oldestPendingStatusIndex() const;
    uint32_t nextPendingSequence();
    void publishDeliveryEvent(const ChatMessage& message,
                              MessageStatus status,
                              uint32_t timestamp_ms = 0,
                              SendFailureKind failure =
                                  SendFailureKind::Unknown);
    void publishDeliveryEventForProtocol(
        MessageId msg_id,
        MeshProtocol protocol,
        MessageStatus status,
        uint32_t timestamp_ms = 0,
        SendFailureKind failure = SendFailureKind::Unknown);

    ChatModel& model_;
    IChatStore& store_;
    IChatDeliveryEventPort* delivery_event_port_ = nullptr;
    std::array<PendingOutboundWrite, kPendingOutboundWriteDepth>
        pending_outbound_writes_{};
    std::array<PendingStatusWrite, kPendingStatusWriteDepth>
        pending_status_writes_{};
    std::array<TrackedOutbound, kTrackedOutboundDepth>
        tracked_outbound_{};
    uint32_t next_pending_sequence_ = 1;
};

} // namespace chat::delivery
