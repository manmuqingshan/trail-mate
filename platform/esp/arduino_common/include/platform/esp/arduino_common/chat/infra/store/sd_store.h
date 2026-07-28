/**
 * @file sd_store.h
 * @brief Protocol-partitioned append-only ESP chat storage.
 */

#pragma once

#include "chat/ports/i_chat_store.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/chat/infra/store/protocol_chat_codec.h"
#include "platform/esp/arduino_common/memory/psram_allocator.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/storage/storage_contracts.h"

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

    bool isReady() const override
    {
        return ready_.load(std::memory_order_acquire);
    }
    bool isHydrating() const
    {
        return hydrating_.load(std::memory_order_acquire);
    }
    bool compactionPending() const
    {
        return maintenance_compaction_requested_.load(
            std::memory_order_acquire);
    }
    bool persistencePending() const
    {
        return persistence_pending_.load(std::memory_order_acquire);
    }

    // Construction is intentionally empty. Disk recovery is an explicit
    // background lifecycle step so AppContext can become interactive first.
    // Same operation/generation resumes the current maintenance cursor after
    // a retryable physical SD/device transaction miss; logical maintenance
    // ownership remains with this operation until completion or cancellation.
    // A new generation starts fresh.
    platform::esp::common::storage::StorageOperationResult beginMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation);
    platform::esp::common::storage::StorageOperationResult stepMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation,
        const platform::esp::common::storage::StorageOperationBudget& budget);
    void cancelMaintenance(
        platform::esp::common::storage::StorageOperation operation,
        platform::esp::common::storage::StorageOperationGeneration generation);

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
    static constexpr std::size_t kPendingStatusProjectionCapacity = 32;

    bool appendInternal(const ChatMessage& msg, bool incoming_commit);
    bool ensureLayout() const;
    bool ensureProtocolLayout(MeshProtocol protocol) const;
    bool recoverProjectionSnapshot(MeshProtocol protocol,
                                   const char* base_name);
    enum class ReconcileStepResult : uint8_t
    {
        InProgress,
        Complete,
        Failed,
    };
    enum class ConversationReconcilePhase : uint8_t
    {
        ScanSegments,
        RepairSegment,
        ReadLatest,
        ScanUnread,
        Commit,
    };
    ReconcileStepResult stepConversationDirectoryReconcile(
        MeshProtocol protocol,
        const char* directory_name);
    ReconcileStepResult stepProtocolCatalogReconcile(MeshProtocol protocol);
    ReconcileStepResult stepSeenRebuild();
    bool beginSeenRebuild();

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
    bool queueStatusProjectionLocked(
        const ProtocolStatusProjection& projection);
    void prunePendingStatusProjectionsLocked();
    void refreshPersistenceDemandLocked();
    bool flushPendingStatusProjections(std::size_t budget);
    bool appendSeenProjection(
        const storage::v2::ReticulumSeenProjection& projection) const;
    bool rememberReticulumHash(const uint8_t* hash);
    bool acquirePersistenceLease(TickType_t wait_ticks);
    void releasePersistenceLease();
    void releaseMaintenanceLease();
    void resetCatalogReconcileCursor();

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

    enum class MaintenancePhase : uint8_t
    {
        Idle,
        HydrationPrepare,
        HydrationRecover,
        HydrationJournal,
        HydrationReconcile,
        HydrationSeen,
        HydrationRebuildSeen,
        PersistenceFlush,
        CompactionPrepare,
        CompactionInspect,
        CompactionCreate,
        CompactionWrite,
        CompactionReplace,
        CompactionRemove,
        CompactionAdvance,
        Complete,
        Failed,
    };

    struct MaintenanceState
    {
        MaintenancePhase phase = MaintenancePhase::Idle;
        platform::esp::common::storage::StorageOperation operation =
            platform::esp::common::storage::StorageOperation::None;
        platform::esp::common::storage::StorageOperationGeneration generation =
            0U;
        uint8_t protocol_index = 0U;
        uint8_t journal_index = 0U;
        uint8_t recovery_index = 0U;
        bool journal_started = false;
        bool seen_journal_found = false;
        bool seen_rebuild_required = false;
        uint8_t compaction_projection_index = 0U;
        uint8_t compaction_inspection_index = 0U;
        uint32_t compaction_record_index = 0U;
        bool compact_catalog = false;
        bool compact_read = false;
        bool compact_status = false;
    };

    bool prepareMaintenanceJournal();
    bool applyHydrationJournalSlot(MeshProtocol protocol,
                                   storage::v2::JournalKind kind);
    bool advanceHydrationJournal();
    bool recoverHydrationSnapshot();
    bool resetHydrationState();
    platform::esp::common::storage::StorageOperationResult stepHydration(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult stepPersistence(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult stepCompaction(
        const platform::esp::common::storage::StorageOperationBudget& budget);
    platform::esp::common::storage::StorageOperationResult maintenanceFailure(
        platform::esp::common::storage::StorageOperationResultKind kind) const;
    static const char* hydrationRecoveryName(uint8_t index);

    storage::v2::FixedSlotJournalEngine journal_{};
    storage::v2::FixedSlotJournalCursor maintenance_journal_{};
    mutable SemaphoreHandle_t mutex_ = nullptr;
    mutable SemaphoreHandle_t persistence_mutex_ = nullptr;
    bool maintenance_persistence_locked_ = false;
    ::platform::esp::arduino_common::storage::SdRuntimeDir
        maintenance_directory_{};
    bool maintenance_directory_open_ = false;
    CatalogList catalog_{};
    ReadStateList read_state_{};
    StatusList statuses_{};
    StatusList pending_status_projections_{};
    std::size_t pending_status_head_ = 0U;
    uint32_t pending_status_revision_ = 0U;
    StatusList flush_status_batch_{};
    mutable SeenList seen_hot_{};
    mutable ScratchBuffer scratch_{};
    mutable ScratchBuffer maintenance_scratch_{};
    ChatMessage maintenance_seen_message_{};
    storage::v2::ChatCatalogProjection maintenance_seen_catalog_{};
    ConversationId maintenance_seen_conversation_{};
    uint32_t maintenance_seen_catalog_index_ = 0U;
    uint32_t maintenance_seen_message_count_ = 0U;
    uint32_t maintenance_seen_message_ordinal_ = 0U;
    bool maintenance_seen_rebuild_started_ = false;
    char maintenance_reconcile_name_[80] = {};
    char maintenance_reconcile_directory_path_[128] = {};
    ChatMessage maintenance_reconcile_latest_message_{};
    storage::v2::ChatCatalogProjection
        maintenance_reconcile_projection_{};
    bool maintenance_reconcile_conversation_active_ = false;
    ConversationReconcilePhase maintenance_reconcile_phase_ =
        ConversationReconcilePhase::ScanSegments;
    uint32_t maintenance_reconcile_segment_ = 0U;
    uint32_t maintenance_reconcile_total_count_ = 0U;
    uint32_t maintenance_reconcile_last_segment_ = 0U;
    uint32_t maintenance_reconcile_last_segment_count_ = 0U;
    uint32_t maintenance_reconcile_unread_ordinal_ = 0U;
    uint32_t maintenance_reconcile_unread_count_ = 0U;
    bool maintenance_reconcile_found_segment_ = false;
    bool maintenance_reconcile_catalog_current_ = false;
    CatalogList compaction_catalog_{};
    ReadStateList compaction_read_state_{};
    StatusList compaction_statuses_{};
    MaintenanceState maintenance_{};
    char maintenance_path_[128] = {};
    char maintenance_final_path_[128] = {};
    char maintenance_backup_path_[128] = {};
    char maintenance_delta_path_[128] = {};
    MeshProtocol maintenance_protocol_ = MeshProtocol::Meshtastic;
    storage::v2::JournalKind maintenance_kind_ =
        storage::v2::JournalKind::MessageSegment;
    std::size_t maintenance_slot_size_ = 0U;
    bool projection_dirty_[3] = {};
    std::atomic<bool> ready_{false};
    std::atomic<bool> hydrating_{false};
    std::atomic<bool> persistence_pending_{false};
    mutable std::atomic<bool> maintenance_compaction_requested_{false};
};

} // namespace chat
