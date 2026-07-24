/**
 * @file sd_store.h
 * @brief Protocol-partitioned append-only ESP chat storage.
 */

#pragma once

#include "chat/ports/i_chat_store.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"
#include "platform/esp/arduino_common/memory/psram_allocator.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat
{

class SdStore final : public IChatStore
{
  public:
    static constexpr const char* kRoot = "/data/v2";
    static constexpr const char* kMeshtasticRoot = "/data/v2/mt/chat";
    static constexpr const char* kMeshCoreRoot = "/data/v2/mc/chat";
    static constexpr const char* kReticulumRoot = "/data/v2/rt/chat";
    static constexpr std::size_t kMessageSegmentBytes = 128U * 1024U;
    static constexpr std::size_t kCatalogPreviewLen =
        storage::v2::kCatalogPreviewMax;
    static constexpr std::size_t kSeenHotCapacity = 4096;

    SdStore();
    ~SdStore() override;

    bool isReady() const { return ready_.load(std::memory_order_acquire); }
    bool isHydrating() const
    {
        return hydrating_.load(std::memory_order_acquire);
    }

    // Construction is intentionally empty. Disk recovery is an explicit
    // background lifecycle step so AppContext can become interactive first.
    bool hydrateFromStorage();
    bool compactDeferred();

    void append(const ChatMessage& msg) override;
    bool appendDurably(const ChatMessage& msg) override;
    bool appendIncomingDurably(const ChatMessage& msg) override;
    std::vector<ChatMessage> loadRecent(const ConversationId& conv,
                                        std::size_t n) override;
    std::vector<ChatMessage> loadPageFromLatest(
        const ConversationId& conv,
        std::size_t offset_from_latest,
        std::size_t limit,
        std::size_t* total) override;
    std::vector<ConversationMeta> loadConversationPage(
        std::size_t offset,
        std::size_t limit,
        std::size_t* total) override;
    std::vector<ConversationMeta> loadConversationPageForProtocol(
        MeshProtocol protocol,
        std::size_t offset,
        std::size_t limit,
        std::size_t* total) override;
    bool setUnread(const ConversationId& conv, int unread) override;
    int getUnread(const ConversationId& conv) const override;
    void clearConversation(const ConversationId& conv) override;
    void clearAll() override;
    bool updateMessageStatus(MessageId msg_id,
                             MessageStatus status) override;
    bool updateMessageStatusForProtocol(MessageId msg_id,
                                        MeshProtocol protocol,
                                        MessageStatus status) override;
    bool getMessage(MessageId msg_id, ChatMessage* out) const override;
    bool getMessageForProtocol(MessageId msg_id,
                               MeshProtocol protocol,
                               ChatMessage* out) const override;
    bool hasReticulumLxmfMessageHash(
        const uint8_t* lxmf_hash) const override;
    void flush() override;

  private:
    using CatalogList = std::vector<
        storage::v2::ChatCatalogProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ChatCatalogProjection>>;
    using ReadStateList = std::vector<
        storage::v2::ChatReadProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ChatReadProjection>>;
    struct ProtocolStatusProjection
    {
        MeshProtocol protocol = MeshProtocol::Meshtastic;
        storage::v2::ChatStatusProjection value{};
    };

    using StatusList = std::vector<
        ProtocolStatusProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            ProtocolStatusProjection>>;
    using SeenList = std::vector<
        storage::v2::ReticulumSeenProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ReticulumSeenProjection>>;
    using ScratchBuffer = std::vector<
        uint8_t,
        ::platform::esp::arduino_common::memory::PsramAllocator<uint8_t>>;

    static constexpr std::size_t kScratchCapacity = 512;
    static constexpr std::size_t kCatalogCompactThreshold = 512;
    static constexpr std::size_t kReadCompactThreshold = 256;
    static constexpr std::size_t kStatusCompactThreshold = 2048;
    static constexpr uint32_t kProjectionRetryIntervalMs = 5000U;

    bool appendInternal(const ChatMessage& msg, bool incoming_commit);
    bool ensureLayout() const;
    bool ensureProtocolLayout(MeshProtocol protocol) const;
    bool loadRuntimeState();
    bool loadProtocolState(MeshProtocol protocol);
    bool loadCatalogJournal(MeshProtocol protocol, const char* name);
    bool loadReadJournal(MeshProtocol protocol, const char* name);
    bool loadStatusJournal(MeshProtocol protocol, const char* name);
    bool loadSeenJournal();
    bool rebuildSeenJournalFromMessages();
    bool recoverProjectionSnapshot(MeshProtocol protocol,
                                   const char* base_name);
    bool reconcileProtocolCatalog(MeshProtocol protocol);
    bool reconcileConversationDirectory(MeshProtocol protocol,
                                        const char* directory_name);

    std::size_t slotsPerMessageSegment(MeshProtocol protocol) const;
    uint32_t messageCountOnDisk(const ConversationId& conv) const;
    bool readMessageByOrdinal(const ConversationId& conv,
                              uint32_t ordinal,
                              ChatMessage& out_message,
                              uint32_t* out_sequence = nullptr) const;
    bool latestStoredMessage(const ConversationId& conv,
                             ChatMessage& out_message,
                             uint32_t* out_count = nullptr) const;
    bool storedMessageMatches(const ChatMessage& message,
                              bool* out_matches,
                              uint32_t* out_count) const;
    bool appendMessageRecord(const ChatMessage& message,
                             uint32_t sequence);
    bool repairPartialJournal(const char* path,
                              MeshProtocol protocol,
                              storage::v2::JournalKind kind,
                              std::size_t slot_size) const;

    bool appendCatalogProjection(
        const storage::v2::ChatCatalogProjection& projection);
    bool appendReadProjection(
        const storage::v2::ChatReadProjection& projection);
    bool appendStatusProjection(MeshProtocol protocol,
                                const storage::v2::ChatStatusProjection& projection);
    bool appendSeenProjection(
        const storage::v2::ReticulumSeenProjection& projection) const;
    bool rememberReticulumHash(const uint8_t* hash);

    storage::v2::ChatCatalogProjection* findCatalog(
        const ConversationId& conversation);
    const storage::v2::ChatCatalogProjection* findCatalog(
        const ConversationId& conversation) const;
    storage::v2::ChatReadProjection* findReadState(
        const ConversationId& conversation);
    const storage::v2::ChatReadProjection* findReadState(
        const ConversationId& conversation) const;
    storage::v2::ChatStatusProjection* findStatus(MessageId message_id,
                                                  MeshProtocol protocol);
    const storage::v2::ChatStatusProjection* findStatus(
        MessageId message_id,
        MeshProtocol protocol) const;
    void applyStoredStatus(ChatMessage& message) const;
    uint32_t countUnreadAfter(const ConversationId& conversation,
                              uint32_t last_read_sequence) const;
    uint32_t sequenceForUnread(const ConversationId& conversation,
                               uint32_t unread) const;

    bool rewriteCatalogSnapshot(MeshProtocol protocol);
    bool rewriteReadSnapshot(MeshProtocol protocol);
    bool rewriteStatusSnapshot(MeshProtocol protocol);
    bool compactProtocolProjections(MeshProtocol protocol);
    bool rewriteJournalFromCatalog(MeshProtocol protocol,
                                   const char* final_path,
                                   const char* temp_path);
    bool rewriteJournalFromReadState(MeshProtocol protocol,
                                     const char* final_path,
                                     const char* temp_path);
    bool rewriteJournalFromStatus(MeshProtocol protocol,
                                  const char* final_path,
                                  const char* temp_path);

    static MeshProtocol normalizeProtocol(MeshProtocol protocol);
    static const char* protocolRoot(MeshProtocol protocol);
    static const char* protocolSlug(MeshProtocol protocol);
    static bool sameProtocol(MeshProtocol lhs, MeshProtocol rhs);
    static bool sameStoredMessage(const ChatMessage& lhs,
                                  const ChatMessage& rhs);
    static ConversationMeta makeMeta(
        const storage::v2::ChatCatalogProjection& projection);
    static void buildConversationDirectory(const ConversationId& conversation,
                                           char* out,
                                           std::size_t out_len);
    static void buildMessageSegmentPath(const ConversationId& conversation,
                                        uint32_t segment,
                                        char* out,
                                        std::size_t out_len);
    static void buildProjectionPath(MeshProtocol protocol,
                                    const char* name,
                                    char* out,
                                    std::size_t out_len);
    static bool ensureDirectory(const char* path);
    static bool removeTree(const char* path);

    storage::v2::FixedSlotJournalEngine journal_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    CatalogList catalog_{};
    ReadStateList read_state_{};
    StatusList statuses_{};
    mutable SeenList seen_hot_{};
    mutable ScratchBuffer scratch_{};
    bool projection_dirty_[3] = {};
    uint8_t flush_protocol_cursor_ = 0;
    uint32_t last_projection_retry_ms_ = 0;
    std::atomic<bool> ready_{false};
    std::atomic<bool> hydrating_{false};
};

} // namespace chat
