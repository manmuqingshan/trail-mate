/**
 * @file sd_store.h
 * @brief ESP Arduino chat store backed by the mounted SD card.
 */

#pragma once

#include "chat/ports/i_chat_store.h"

#include <map>
#include <vector>

namespace chat
{

class SdStore final : public IChatStore
{
  public:
    static constexpr size_t kMaxMessagesTotal = 120;

    explicit SdStore(const char* path = "/chat_messages.bin");
    ~SdStore() override = default;

    bool isReady() const { return ready_; }

    void append(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(const ConversationId& conv, size_t n) override;
    std::vector<ConversationMeta> loadConversationPage(size_t offset,
                                                       size_t limit,
                                                       size_t* total) override;
    void setUnread(const ConversationId& conv, int unread) override;
    int getUnread(const ConversationId& conv) const override;
    void clearConversation(const ConversationId& conv) override;
    void clearAll() override;
    bool updateMessageStatus(MessageId msg_id, MessageStatus status) override;
    bool getMessage(MessageId msg_id, ChatMessage* out) const override;
    void flush() override;

  private:
    struct StoredMessageEntry
    {
        ChatMessage message;
        uint32_t sequence = 0;
    };

    struct ConversationStorage
    {
        std::vector<StoredMessageEntry> messages;
        int unread_count = 0;
    };

    struct FileHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t conversation_count = 0;
        uint32_t next_sequence = 1;
    } __attribute__((packed));

    struct LegacyFileHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t conversation_count = 0;
    } __attribute__((packed));

    struct ConversationRecord
    {
        uint8_t protocol = 0;
        uint8_t channel = 0;
        uint16_t reserved = 0;
        uint32_t peer = 0;
        int32_t unread_count = 0;
        uint16_t message_count = 0;
        uint16_t reserved2 = 0;
    } __attribute__((packed));

    struct MessageRecord
    {
        uint8_t protocol = 0;
        uint8_t channel = 0;
        uint8_t status = 0;
        uint8_t flags = 0;
        uint32_t from = 0;
        uint32_t peer = 0;
        uint32_t msg_id = 0;
        uint32_t timestamp = 0;
        uint32_t sequence = 0;
        uint8_t team_location_icon = 0;
        int32_t geo_lat_e7 = 0;
        int32_t geo_lon_e7 = 0;
        uint16_t text_len = 0;
        char text[220] = {};
    } __attribute__((packed));

    struct LegacyMessageRecord
    {
        uint8_t protocol = 0;
        uint8_t channel = 0;
        uint8_t status = 0;
        uint8_t flags = 0;
        uint32_t from = 0;
        uint32_t peer = 0;
        uint32_t msg_id = 0;
        uint32_t timestamp = 0;
        uint8_t team_location_icon = 0;
        int32_t geo_lat_e7 = 0;
        int32_t geo_lon_e7 = 0;
        uint16_t text_len = 0;
        char text[220] = {};
    } __attribute__((packed));

    static constexpr uint32_t kMagic = 0x54534D43; // CMST
    static constexpr uint16_t kVersion = 2;

    bool ensureFs() const;
    bool loadFromFs();
    bool saveToFs() const;
    void markDirty();
    void maybeSave(bool force = false);
    void evictOldestMessage();
    ConversationStorage& getConversationStorage(const ConversationId& conv);
    const ConversationStorage& getConversationStorage(const ConversationId& conv) const;

    const char* path_ = nullptr;
    std::map<ConversationId, ConversationStorage> conversations_;
    uint32_t next_sequence_ = 1;
    size_t total_message_count_ = 0;
    mutable uint32_t last_save_ms_ = 0;
    mutable uint32_t dirty_since_ms_ = 0;
    mutable size_t pending_write_count_ = 0;
    mutable bool dirty_ = false;
    bool ready_ = false;

    static constexpr uint32_t kSaveIntervalMs = 1500;
    static constexpr size_t kMaxPendingWrites = 4;
};

} // namespace chat
