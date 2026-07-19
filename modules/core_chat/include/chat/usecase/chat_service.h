/**
 * @file chat_service.h
 * @brief Chat service (use case layer)
 */

#pragma once

#include "../domain/chat_model.h"
#include "../domain/chat_types.h"
#include "../ports/i_chat_store.h"
#include "../ports/i_mesh_adapter.h"
#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/delivery/chat_message_ledger.h"
#include "chat/read/chat_read_state_ledger.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace chat
{

/**
 * @brief Chat service
 * Use case layer: coordinates domain model, adapters, and storage
 */
class ChatService
{
  public:
    class IncomingTextObserver
    {
      public:
        virtual ~IncomingTextObserver() = default;
        virtual void onIncomingText(const MeshIncomingText& msg) = 0;
    };

    class IncomingMessageObserver
    {
      public:
        virtual ~IncomingMessageObserver() = default;
        virtual void onIncomingMessage(const ChatMessage& msg, const RxMeta* rx_meta) = 0;
    };

    class OutgoingTextObserver
    {
      public:
        virtual ~OutgoingTextObserver() = default;
        virtual void onOutgoingText(const MeshIncomingText& msg) = 0;
    };

    class IncomingDataObserver
    {
      public:
        virtual ~IncomingDataObserver() = default;
        virtual void onIncomingData(const MeshIncomingData& msg) = 0;
    };

    ChatService(ChatModel& model,
                IMeshAdapter& adapter,
                IChatStore& store,
                MeshProtocol active_protocol = MeshProtocol::Meshtastic);

    /**
     * @brief Send text message
     * @param channel Channel ID
     * @param text Message text
     * @return Message ID if queued successfully, 0 on failure
     */
    MessageId sendText(ChannelId channel, const std::string& text, NodeId peer = 0);
    MessageId sendTextWithId(ChannelId channel, const std::string& text,
                             MessageId forced_msg_id, NodeId peer = 0);
    MeshSendResult sendTextDetailed(ChannelId channel, const std::string& text,
                                    NodeId peer = 0);
    MeshSendResult sendTextWithIdDetailed(ChannelId channel, const std::string& text,
                                          MessageId forced_msg_id, NodeId peer = 0);
    bool canSendToConversation(const ConversationId& conversation) const;
    MessageId sendTextToConversation(const ConversationId& conversation,
                                     const std::string& text);
    MeshSendResult sendTextToConversationDetailed(const ConversationId& conversation,
                                                  const std::string& text);

    /**
     * @brief Trigger protocol discovery action (if supported by active adapter)
     */
    bool triggerDiscoveryAction(MeshDiscoveryAction action);
    MeshActionResult triggerDiscoveryActionDetailed(MeshDiscoveryAction action);
    MeshActionResult startReticulumAudioCall(
        const ReticulumPeerIdentity& destination);
    MeshActionResult pingReticulumDestination(
        const ReticulumPeerIdentity& destination);
    MeshActionResult persistReticulumPeer(const ReticulumPeerIdentity& destination,
                                          bool favorite);

    /**
     * @brief Switch to channel
     * @param channel Channel ID
     */
    void switchChannel(ChannelId channel);

    /**
     * @brief Mark conversation as read
     * @param conv Conversation ID
     */
    bool markConversationRead(const ConversationId& conv);

    /**
     * @brief Resend failed message
     * @param msg_id Message ID
     * @return true if queued for resend
     */
    bool resendFailed(MessageId msg_id);
    bool resendFailedForProtocol(MessageId msg_id, MeshProtocol protocol);

    /**
     * @brief Get recent messages for a conversation
     */
    std::vector<ChatMessage> getRecentMessages(const ConversationId& conv, size_t limit) const;
    std::vector<ChatMessage> getMessagePageFromLatest(const ConversationId& conv,
                                                      size_t offset_from_latest,
                                                      size_t limit,
                                                      size_t* total) const;
    std::vector<ConversationMeta> getConversations(size_t offset, size_t limit, size_t* total) const;
    int getTotalUnread() const;

    /**
     * @brief Enable/disable in-memory model updates
     */
    void setModelEnabled(bool enabled);
    bool isModelEnabled() const { return model_enabled_; }

    /**
     * @brief Clear all stored messages and model state
     */
    void clearAllMessages();

    /**
     * @brief Clear one conversation from model and backing store
     */
    void clearConversation(const ConversationId& conv);

    /**
     * @brief Process incoming messages (call from mesh task)
     */
    void processIncoming();
    void flushStore();

    void addIncomingTextObserver(IncomingTextObserver* observer);
    void removeIncomingTextObserver(IncomingTextObserver* observer);

    void addIncomingMessageObserver(IncomingMessageObserver* observer);
    void removeIncomingMessageObserver(IncomingMessageObserver* observer);

    void addOutgoingTextObserver(OutgoingTextObserver* observer);
    void removeOutgoingTextObserver(OutgoingTextObserver* observer);

    void addIncomingDataObserver(IncomingDataObserver* observer);
    void removeIncomingDataObserver(IncomingDataObserver* observer);

    /**
     * @brief Handle send result (ack/timeout)
     * @param msg_id Message ID
     * @param ok true if sent successfully
     */
    void handleSendResult(MessageId msg_id, bool ok);

    /**
     * @brief Apply an outbound delivery state update.
     *
     * Queued, Sent, Delivered, and Failed are accepted. Delivered is terminal;
     * an earlier failure may still be superseded by a later valid proof.
     */
    void handleSendResult(MessageId msg_id,
                          MessageStatus status,
                          uint32_t timestamp_ms = 0,
                          delivery::SendFailureKind failure =
                              delivery::SendFailureKind::Unknown);
    void handleSendResultForProtocol(MessageId msg_id,
                                     MeshProtocol protocol,
                                     MessageStatus status,
                                     uint32_t timestamp_ms = 0,
                                     delivery::SendFailureKind failure =
                                         delivery::SendFailureKind::Unknown);

    /**
     * @brief Get message by ID (for UI send status)
     */
    const ChatMessage* getMessage(MessageId msg_id) const;
    const ChatMessage* getMessageForProtocol(MessageId msg_id,
                                             MeshProtocol protocol) const;

    void setDeliveryEventPort(
        delivery::IChatDeliveryEventPort* delivery_event_port);

    void setActiveProtocol(MeshProtocol protocol)
    {
        active_protocol_ = protocol;
    }

    MeshProtocol getActiveProtocol() const
    {
        return active_protocol_;
    }

    /**
     * @brief Get current channel
     */
    ChannelId getCurrentChannel() const
    {
        return current_channel_;
    }

  private:
    struct IncomingIdentity
    {
        MeshProtocol protocol = MeshProtocol::Meshtastic;
        ChannelId channel = ChannelId::PRIMARY;
        NodeId from = 0;
        NodeId peer = 0;
        MessageId msg_id = 0;
        bool has_reticulum_destination = false;
        uint8_t reticulum_destination_hash[kReticulumPeerHashSize] = {};
        bool has_reticulum_lxmf_hash = false;
        uint8_t reticulum_lxmf_hash[kReticulumLxmfHashSize] = {};

        bool operator==(const IncomingIdentity& other) const
        {
            if (protocol != other.protocol ||
                channel != other.channel)
            {
                return false;
            }
            if (protocol == MeshProtocol::Reticulum &&
                has_reticulum_lxmf_hash &&
                other.has_reticulum_lxmf_hash)
            {
                return std::memcmp(reticulum_lxmf_hash,
                                   other.reticulum_lxmf_hash,
                                   kReticulumLxmfHashSize) == 0;
            }
            if (msg_id != other.msg_id)
            {
                return false;
            }
            if (protocol == MeshProtocol::Reticulum &&
                has_reticulum_destination &&
                other.has_reticulum_destination)
            {
                return std::memcmp(reticulum_destination_hash,
                                   other.reticulum_destination_hash,
                                   kReticulumPeerHashSize) == 0;
            }
            if (has_reticulum_destination != other.has_reticulum_destination)
            {
                return false;
            }
            return from == other.from && peer == other.peer;
        }
    };

    static constexpr std::size_t kRecentIncomingLimit = 256;

    struct RecentIncomingWindow
    {
        void clear();
        [[nodiscard]] bool contains(const IncomingIdentity& identity) const;
        void remember(const IncomingIdentity& identity);

        std::array<IncomingIdentity, kRecentIncomingLimit> entries{};
        std::size_t next = 0;
        std::size_t count = 0;
    };

    ChatModel& model_;
    IMeshAdapter& adapter_;
    IChatStore& store_;
    delivery::ChatMessageLedger message_ledger_;
    read::ChatReadStateLedger read_state_ledger_;
    ChannelId current_channel_;
    bool model_enabled_ = true;
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;
    mutable ChatMessage store_lookup_cache_{};
    RecentIncomingWindow recent_incoming_{};

    std::vector<IncomingTextObserver*> incoming_text_observers_;
    std::vector<IncomingMessageObserver*> incoming_message_observers_;
    std::vector<OutgoingTextObserver*> outgoing_text_observers_;
    std::vector<IncomingDataObserver*> incoming_data_observers_;

    [[nodiscard]] bool isDuplicateIncoming(const ChatMessage& msg) const;
    void rememberIncoming(const ChatMessage& msg);
    MeshSendResult sendTextResolvedDetailed(
        ChannelId channel,
        const std::string& text,
        MessageId forced_msg_id,
        NodeId peer,
        const ReticulumPeerIdentity* reticulum_destination);
};

} // namespace chat
