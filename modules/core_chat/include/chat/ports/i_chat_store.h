/**
 * @file i_chat_store.h
 * @brief Chat storage interface
 */

#pragma once

#include "../domain/chat_types.h"
#include <vector>

namespace chat
{

/**
 * @brief Chat storage interface
 * Abstracts storage implementation (RAM, Flash, etc.)
 */
class IChatStore
{
  public:
    virtual ~IChatStore() = default;

    /**
     * @brief Append message to storage
     * @param msg Message to append
     */
    virtual void append(const ChatMessage& msg) = 0;

    /**
     * Persist an incoming message and any durable delivery identity it owns.
     *
     * Stores that cannot fail independently may inherit this implementation.
     * Persistent stores should override it and return false unless both the
     * message and its deduplication identity are durable.
     */
    virtual bool appendIncomingDurably(const ChatMessage& msg)
    {
        append(msg);
        return true;
    }

    /**
     * @brief Load recent messages for a conversation
     * @param conv Conversation ID
     * @param n Number of messages to load
     * @return Vector of messages (oldest first)
     */
    virtual std::vector<ChatMessage> loadRecent(const ConversationId& conv, size_t n) = 0;

    /**
     * @brief Load one message page counted backwards from the newest message
     * @param conv Conversation ID
     * @param offset_from_latest Number of newer messages to skip (0 means newest page)
     * @param limit Max messages to return
     * @param total Optional total message count for the conversation
     * @return Vector of messages in chronological order (oldest first)
     */
    virtual std::vector<ChatMessage> loadPageFromLatest(const ConversationId& conv,
                                                        size_t offset_from_latest,
                                                        size_t limit,
                                                        size_t* total)
    {
        if (limit == 0)
        {
            if (total)
            {
                *total = 0;
            }
            return {};
        }

        const size_t window_limit = offset_from_latest + limit;
        std::vector<ChatMessage> window = loadRecent(conv, window_limit);
        if (total)
        {
            *total = window.size();
        }
        if (offset_from_latest >= window.size())
        {
            return {};
        }

        const size_t end = window.size() - offset_from_latest;
        const size_t start = (end > limit) ? (end - limit) : 0;
        return std::vector<ChatMessage>(window.begin() + static_cast<long>(start),
                                        window.begin() + static_cast<long>(end));
    }

    /**
     * @brief Load conversation list metadata
     * @param offset Start offset (pagination)
     * @param limit Max items to return (0 means all)
     * @param total Optional total count out-parameter
     * @return Vector of conversation meta items
     */
    virtual std::vector<ConversationMeta> loadConversationPage(size_t offset,
                                                               size_t limit,
                                                               size_t* total) = 0;

    /**
     * @brief Set unread count for conversation
     * @param conv Conversation ID
     * @param unread Unread count
     */
    virtual bool setUnread(const ConversationId& conv, int unread) = 0;

    /**
     * @brief Get unread count for conversation
     * @param conv Conversation ID
     * @return Unread count
     */
    virtual int getUnread(const ConversationId& conv) const = 0;

    /**
     * @brief Clear all messages for conversation
     * @param conv Conversation ID
     */
    virtual void clearConversation(const ConversationId& conv) = 0;

    /**
     * @brief Clear all messages for all channels
     */
    virtual void clearAll() = 0;

    /**
     * @brief Update stored message status by message ID
     * @param msg_id Message ID
     * @param status New status
     * @return true if updated
     */
    virtual bool updateMessageStatus(MessageId msg_id, MessageStatus status) = 0;
    virtual bool updateMessageStatusForProtocol(MessageId msg_id,
                                                MeshProtocol protocol,
                                                MessageStatus status)
    {
        (void)msg_id;
        (void)protocol;
        (void)status;
        return false;
    }

    /**
     * @brief Look up a stored message by message ID
     * @param msg_id Message ID
     * @param out Optional out-parameter populated on success
     * @return true if found
     */
    virtual bool getMessage(MessageId msg_id, ChatMessage* out) const = 0;
    virtual bool getMessageForProtocol(MessageId msg_id,
                                       MeshProtocol protocol,
                                       ChatMessage* out) const
    {
        (void)msg_id;
        (void)protocol;
        (void)out;
        return false;
    }

    /**
     * @brief Check whether an LXMF message hash has already been stored.
     *
     * The default implementation returns false for stores that do not maintain
     * Reticulum/LXMF durable identity state.
     */
    virtual bool hasReticulumLxmfMessageHash(const uint8_t* lxmf_hash) const
    {
        (void)lxmf_hash;
        return false;
    }

    /**
     * @brief Flush pending buffered writes to persistent storage
     *
     * Default implementation is a no-op for stores that do not buffer.
     */
    virtual void flush() {}
};

} // namespace chat
