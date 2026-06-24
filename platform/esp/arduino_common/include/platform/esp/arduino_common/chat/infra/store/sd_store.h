/**
 * @file sd_store.h
 * @brief ESP Arduino chat store backed by the mounted SD card.
 */

#pragma once

#include "chat/ports/i_chat_store.h"

#include <vector>

namespace platform::esp::arduino_common::storage
{
class SdRuntimeFile;
} // namespace platform::esp::arduino_common::storage

namespace chat
{

class SdStore final : public IChatStore
{
  public:
    static constexpr const char* kDir = "/chat";
    static constexpr const char* kIndexFile = "/chat/index.bin";
    static constexpr size_t kMaxMessagesPerConv = 100;
    static constexpr size_t kMaxTextLen = 233;
    static constexpr size_t kPreviewLen = 48;

    SdStore();
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
    struct FileHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t head = 0;
        uint16_t count = 0;
        uint16_t reserved = 0;
    } __attribute__((packed));

    struct Record
    {
        uint8_t protocol = 0;
        uint8_t channel = 0;
        uint8_t status = 0;
        uint16_t text_len = 0;
        uint32_t from = 0;
        uint32_t peer = 0;
        uint32_t msg_id = 0;
        uint32_t timestamp = 0;
        char text[kMaxTextLen] = {};
    } __attribute__((packed));

    struct IndexHeader
    {
        uint32_t magic = 0;
        uint16_t version = 0;
        uint16_t count = 0;
    } __attribute__((packed));

    struct IndexEntry
    {
        uint8_t protocol = 0;
        uint8_t channel = 0;
        uint8_t status = 0;
        uint16_t unread = 0;
        uint32_t peer = 0;
        uint32_t last_msg_id = 0;
        uint32_t last_timestamp = 0;
        uint32_t last_from = 0;
        uint16_t preview_len = 0;
        char preview[kPreviewLen] = {};
    } __attribute__((packed));

    static constexpr uint32_t kFileMagic = 0x474F4C43;  // "CLOG"
    static constexpr uint32_t kIndexMagic = 0x54414843; // "CHAT"
    static constexpr uint16_t kVersion = 2;

    bool ensureFs() const;
    bool ensureDir() const;
    bool readIndex(std::vector<IndexEntry>& entries) const;
    bool writeIndex(const std::vector<IndexEntry>& entries) const;
    bool ensureIndex(std::vector<IndexEntry>& entries);
    bool findIndexEntry(const ConversationId& conv,
                        std::vector<IndexEntry>& entries,
                        size_t* out_idx) const;
    bool findIndexEntry(const ConversationId& conv,
                        const std::vector<IndexEntry>& entries,
                        size_t* out_idx) const;
    void updateIndexForMessage(const ChatMessage& msg);
    void rebuildIndex();
    bool loadFileHeader(::platform::esp::arduino_common::storage::SdRuntimeFile& file,
                        FileHeader& header) const;
    bool initFileHeader(::platform::esp::arduino_common::storage::SdRuntimeFile& file) const;
    bool readRecord(::platform::esp::arduino_common::storage::SdRuntimeFile& file,
                    uint16_t slot,
                    Record& rec) const;
    bool writeRecord(::platform::esp::arduino_common::storage::SdRuntimeFile& file,
                     uint16_t slot,
                     const Record& rec) const;
    bool openConversationForUpdate(const ConversationId& conv,
                                   ::platform::esp::arduino_common::storage::SdRuntimeFile& file,
                                   FileHeader& header) const;
    void buildConversationPath(const ConversationId& conv, char* out, size_t out_len) const;
    const char* channelName(ChannelId channel) const;
    static ChatMessage messageFromRecord(const Record& rec);
    static Record recordFromMessage(const ChatMessage& msg);
    static ConversationMeta metaFromIndexEntry(const IndexEntry& entry);
    static bool hasLogSuffix(const char* name);

    bool ready_ = false;
};

} // namespace chat
