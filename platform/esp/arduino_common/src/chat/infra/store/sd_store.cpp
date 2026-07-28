/**
 * @file sd_store.cpp
 * @brief Protocol-partitioned append-only ESP chat storage.
 */

#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"

#include "platform/esp/arduino_common/storage/scoped_state_lock.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <esp_timer.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat
{
namespace
{
namespace storage_runtime = ::platform::esp::arduino_common::storage;
namespace storage_v2 = ::chat::storage::v2;
namespace storage_contracts = ::platform::esp::common::storage;

storage_contracts::StorageOperationResultKind stateLockFailure(
    storage_runtime::StateLockResult result)
{
    return result == storage_runtime::StateLockResult::Unavailable
               ? storage_contracts::StorageOperationResultKind::
                     DeviceUnavailable
               : storage_contracts::StorageOperationResultKind::StateBusy;
}

class ScopedPersistenceLease final
{
  public:
    ScopedPersistenceLease(SemaphoreHandle_t mutex, TickType_t wait_ticks)
        : mutex_(mutex)
    {
        locked_ = mutex_ &&
                  xSemaphoreTakeRecursive(mutex_, wait_ticks) == pdTRUE;
    }

    ~ScopedPersistenceLease()
    {
        if (locked_)
        {
            xSemaphoreGiveRecursive(mutex_);
        }
    }

    ScopedPersistenceLease(const ScopedPersistenceLease&) = delete;
    ScopedPersistenceLease& operator=(const ScopedPersistenceLease&) = delete;

    bool locked() const { return locked_; }

  private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

uint32_t monotonic_millis()
{
#if defined(ARDUINO)
    return millis();
#else
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#endif
}

#if defined(ARDUINO)
#define CHAT_STORE_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define CHAT_STORE_LOG(...) std::printf(__VA_ARGS__)
#endif

constexpr MeshProtocol kProtocols[] = {
    MeshProtocol::Meshtastic,
    MeshProtocol::MeshCore,
    MeshProtocol::Reticulum,
};

constexpr uint8_t kHydrationJournalCount = 6U;
constexpr uint8_t kHydrationRecoveryCount = 3U;
constexpr TickType_t kPersistenceLeaseWaitTicks = pdMS_TO_TICKS(50U);

std::size_t protocolIndex(MeshProtocol protocol)
{
    if (protocol == MeshProtocol::MeshCore)
    {
        return 1;
    }
    if (protocol == MeshProtocol::Reticulum ||
        protocol == MeshProtocol::RNode)
    {
        return 2;
    }
    return 0;
}

bool sameConversationKey(const ConversationId& lhs,
                         const ConversationId& rhs)
{
    const MeshProtocol lhs_protocol =
        lhs.protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                            : lhs.protocol;
    const MeshProtocol rhs_protocol =
        rhs.protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                            : rhs.protocol;
    if (lhs_protocol != rhs_protocol || lhs.channel != rhs.channel)
    {
        return false;
    }
    if (lhs_protocol == MeshProtocol::Reticulum)
    {
        const bool lhs_has_destination =
            hasReticulumDestinationIdentity(lhs.reticulum_identity);
        const bool rhs_has_destination =
            hasReticulumDestinationIdentity(rhs.reticulum_identity);
        if (lhs_has_destination || rhs_has_destination)
        {
            return lhs_has_destination && rhs_has_destination &&
                   sameReticulumDestinationHash(lhs.reticulum_identity,
                                                rhs.reticulum_identity);
        }
    }
    return lhs.peer == rhs.peer;
}

bool copyTextPreview(char* out,
                     std::size_t out_len,
                     const std::string& text)
{
    if (!out || out_len == 0U)
    {
        return false;
    }
    const std::size_t length = std::min(out_len - 1U, text.size());
    if (length != 0U)
    {
        std::memcpy(out, text.data(), length);
    }
    out[length] = '\0';
    return true;
}

bool hasSuffix(const char* value, const char* suffix)
{
    if (!value || !suffix)
    {
        return false;
    }
    const std::size_t value_len = std::strlen(value);
    const std::size_t suffix_len = std::strlen(suffix);
    return value_len >= suffix_len &&
           std::memcmp(value + value_len - suffix_len,
                       suffix,
                       suffix_len) == 0;
}

void hashToHex(const uint8_t* hash, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789abcdef";
    if (!hash || !out || out_len < kReticulumPeerHashSize * 2U + 1U)
    {
        return;
    }
    for (std::size_t index = 0; index < kReticulumPeerHashSize; ++index)
    {
        out[index * 2U] = kHex[(hash[index] >> 4U) & 0x0FU];
        out[index * 2U + 1U] = kHex[hash[index] & 0x0FU];
    }
    out[kReticulumPeerHashSize * 2U] = '\0';
}

} // namespace

SdStore::SdStore()
    : mutex_(xSemaphoreCreateRecursiveMutex())
{
    scratch_.resize(kScratchCapacity);
    maintenance_scratch_.resize(kScratchCapacity);
    catalog_.reserve(64);
    read_state_.reserve(64);
    statuses_.reserve(256);
    pending_status_projections_.reserve(kPendingStatusProjectionCapacity);
    flush_status_batch_.reserve(4);
    seen_hot_.reserve(256);
    persistence_mutex_ = xSemaphoreCreateRecursiveMutex();
    CHAT_STORE_LOG("[ChatStoreV2] constructed ready=0 hydration=pending root=%s\n", kRoot);
}

SdStore::~SdStore()
{
    resetCatalogReconcileCursor();
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
    if (persistence_mutex_)
    {
        vSemaphoreDelete(persistence_mutex_);
        persistence_mutex_ = nullptr;
    }
}

bool SdStore::acquirePersistenceLease(TickType_t wait_ticks)
{
    return persistence_mutex_ &&
           xSemaphoreTakeRecursive(persistence_mutex_, wait_ticks) == pdTRUE;
}

void SdStore::releasePersistenceLease()
{
    if (persistence_mutex_)
    {
        xSemaphoreGiveRecursive(persistence_mutex_);
    }
}

void SdStore::releaseMaintenanceLease()
{
    if (maintenance_persistence_locked_)
    {
        maintenance_persistence_locked_ = false;
        releasePersistenceLease();
    }
}

void SdStore::resetCatalogReconcileCursor()
{
    maintenance_directory_.close();
    maintenance_directory_open_ = false;
    maintenance_reconcile_name_[0] = '\0';
    maintenance_reconcile_directory_path_[0] = '\0';
    maintenance_reconcile_projection_ = {};
    maintenance_reconcile_conversation_active_ = false;
    maintenance_reconcile_phase_ =
        ConversationReconcilePhase::ScanSegments;
    maintenance_reconcile_segment_ = 0U;
    maintenance_reconcile_total_count_ = 0U;
    maintenance_reconcile_last_segment_ = 0U;
    maintenance_reconcile_last_segment_count_ = 0U;
    maintenance_reconcile_unread_ordinal_ = 0U;
    maintenance_reconcile_unread_count_ = 0U;
    maintenance_reconcile_found_segment_ = false;
    maintenance_reconcile_catalog_current_ = false;
}

const char* SdStore::hydrationRecoveryName(uint8_t index)
{
    switch (index)
    {
    case 0U:
        return "catalog";
    case 1U:
        return "read";
    case 2U:
        return "status";
    default:
        return nullptr;
    }
}

bool SdStore::resetHydrationState()
{
    resetCatalogReconcileCursor();
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return false;
    }
    catalog_.clear();
    read_state_.clear();
    statuses_.clear();
    pending_status_projections_.clear();
    pending_status_head_ = 0U;
    pending_status_revision_ = 0U;
    flush_status_batch_.clear();
    refreshPersistenceDemandLocked();
    seen_hot_.clear();
    projection_dirty_[0] = false;
    projection_dirty_[1] = false;
    projection_dirty_[2] = false;
    maintenance_.protocol_index = 0U;
    maintenance_.journal_index = 0U;
    maintenance_.recovery_index = 0U;
    maintenance_.journal_started = false;
    maintenance_.seen_journal_found = false;
    maintenance_.seen_rebuild_required = false;
    maintenance_journal_.reset();
    maintenance_path_[0] = '\0';
    maintenance_seen_catalog_index_ = 0U;
    maintenance_seen_message_count_ = 0U;
    maintenance_seen_message_ordinal_ = 0U;
    maintenance_seen_rebuild_started_ = false;
    maintenance_reconcile_name_[0] = '\0';
    return true;
}

bool SdStore::beginSeenRebuild()
{
    if (maintenance_seen_rebuild_started_)
    {
        return true;
    }
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot",
                        maintenance_final_path_,
                        sizeof(maintenance_final_path_));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot.tmp",
                        maintenance_path_,
                        sizeof(maintenance_path_));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.snapshot.bak",
                        maintenance_backup_path_,
                        sizeof(maintenance_backup_path_));
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.delta",
                        maintenance_delta_path_,
                        sizeof(maintenance_delta_path_));
    (void)storage_runtime::sd_remove(maintenance_path_);
    const std::size_t slot_size = storage_v2::reticulumSeenSlotSize();
    if (!journal_.create(maintenance_path_,
                         MeshProtocol::Reticulum,
                         storage_v2::JournalKind::ReticulumSeen,
                         slot_size))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return false;
    }
    seen_hot_.clear();
    maintenance_seen_catalog_index_ = 0U;
    maintenance_seen_message_count_ = 0U;
    maintenance_seen_message_ordinal_ = 0U;
    maintenance_seen_rebuild_started_ = true;
    return true;
}

SdStore::ReconcileStepResult SdStore::stepSeenRebuild()
{
    if (!beginSeenRebuild())
    {
        return ReconcileStepResult::Failed;
    }

    bool rebuild_complete = false;
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked())
        {
            return ReconcileStepResult::InProgress;
        }
        if (maintenance_seen_catalog_index_ >= catalog_.size())
        {
            rebuild_complete = true;
        }
        else
        {
            maintenance_seen_catalog_ =
                catalog_[maintenance_seen_catalog_index_];
        }
    }
    if (rebuild_complete)
    {
        if (!storage_v2::replaceFileAtomically(maintenance_path_,
                                               maintenance_final_path_,
                                               maintenance_backup_path_))
        {
            return ReconcileStepResult::Failed;
        }
        (void)storage_runtime::sd_remove(maintenance_delta_path_);
        maintenance_seen_rebuild_started_ = false;
        return ReconcileStepResult::Complete;
    }

    if (maintenance_seen_catalog_.deleted ||
        !sameProtocol(maintenance_seen_catalog_.conversation.protocol,
                      MeshProtocol::Reticulum))
    {
        ++maintenance_seen_catalog_index_;
        maintenance_seen_message_count_ = 0U;
        maintenance_seen_message_ordinal_ = 0U;
        return ReconcileStepResult::InProgress;
    }

    if (maintenance_seen_message_count_ == 0U)
    {
        maintenance_seen_conversation_ =
            maintenance_seen_catalog_.conversation;
        maintenance_seen_message_count_ =
            messageCountOnDisk(maintenance_seen_conversation_);
    }
    if (maintenance_seen_message_ordinal_ >=
        maintenance_seen_message_count_)
    {
        ++maintenance_seen_catalog_index_;
        maintenance_seen_message_count_ = 0U;
        maintenance_seen_message_ordinal_ = 0U;
        return ReconcileStepResult::InProgress;
    }

    if (!readMessageByOrdinal(maintenance_seen_conversation_,
                              maintenance_seen_message_ordinal_,
                              maintenance_seen_message_))
    {
        return ReconcileStepResult::Failed;
    }
    if (chat::hasReticulumLxmfMessageHash(maintenance_seen_message_))
    {
        storage_v2::ReticulumSeenProjection projection{};
        std::memcpy(projection.hash,
                    maintenance_seen_message_.reticulum_lxmf_hash,
                    sizeof(projection.hash));
        const std::size_t slot_size = storage_v2::reticulumSeenSlotSize();
        if (!storage_v2::encodeReticulumSeenSlot(projection,
                                                 maintenance_scratch_.data(),
                                                 slot_size) ||
            !journal_.append(maintenance_path_,
                             MeshProtocol::Reticulum,
                             storage_v2::JournalKind::ReticulumSeen,
                             slot_size,
                             maintenance_scratch_.data()))
        {
            return ReconcileStepResult::Failed;
        }
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked())
        {
            return ReconcileStepResult::InProgress;
        }
        if (seen_hot_.size() == kSeenHotCapacity)
        {
            seen_hot_.erase(seen_hot_.begin());
        }
        seen_hot_.push_back(projection);
    }
    ++maintenance_seen_message_ordinal_;
    return ReconcileStepResult::InProgress;
}

SdStore::ReconcileStepResult SdStore::stepProtocolCatalogReconcile(
    MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    if (!maintenance_directory_open_)
    {
        {
            storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
            if (!state_lock.locked())
            {
                return ReconcileStepResult::InProgress;
            }
            for (storage_v2::ChatCatalogProjection& projection : catalog_)
            {
                if (sameProtocol(projection.conversation.protocol, protocol))
                {
                    projection.deleted = true;
                }
            }
            std::snprintf(maintenance_path_,
                          sizeof(maintenance_path_),
                          "%s/conversations",
                          protocolRoot(protocol));
        }
        if (!maintenance_directory_.open(maintenance_path_))
        {
            return ReconcileStepResult::Failed;
        }
        maintenance_directory_open_ = true;
    }

    if (maintenance_reconcile_conversation_active_)
    {
        const ReconcileStepResult result =
            stepConversationDirectoryReconcile(
                protocol,
                maintenance_reconcile_name_);
        if (result == ReconcileStepResult::Complete)
        {
            maintenance_reconcile_conversation_active_ = false;
            return ReconcileStepResult::InProgress;
        }
        if (result == ReconcileStepResult::Failed)
        {
            maintenance_reconcile_conversation_active_ = false;
            storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
            if (state_lock.locked())
            {
                projection_dirty_[protocolIndex(protocol)] = true;
            }
            return ReconcileStepResult::InProgress;
        }
        return result;
    }

    bool is_directory = false;
    if (!maintenance_directory_.read_next(maintenance_reconcile_name_,
                                          sizeof(maintenance_reconcile_name_),
                                          &is_directory))
    {
        maintenance_directory_.close();
        maintenance_directory_open_ = false;
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked())
        {
            return ReconcileStepResult::InProgress;
        }
        catalog_.erase(std::remove_if(catalog_.begin(),
                                      catalog_.end(),
                                      [&](const auto& value)
                                      {
                                          return sameProtocol(
                                                     value.conversation.protocol,
                                                     protocol) &&
                                                 value.deleted;
                                      }),
                       catalog_.end());
        return ReconcileStepResult::Complete;
    }
    if (is_directory && maintenance_reconcile_name_[0] != '\0')
    {
        std::snprintf(maintenance_reconcile_directory_path_,
                      sizeof(maintenance_reconcile_directory_path_),
                      "%s/conversations/%s",
                      protocolRoot(protocol),
                      maintenance_reconcile_name_);
        maintenance_reconcile_conversation_active_ = true;
        maintenance_reconcile_phase_ =
            ConversationReconcilePhase::ScanSegments;
        maintenance_reconcile_projection_ = {};
        maintenance_reconcile_segment_ = 0U;
        maintenance_reconcile_total_count_ = 0U;
        maintenance_reconcile_last_segment_ = 0U;
        maintenance_reconcile_last_segment_count_ = 0U;
        maintenance_reconcile_unread_ordinal_ = 0U;
        maintenance_reconcile_unread_count_ = 0U;
        maintenance_reconcile_found_segment_ = false;
        maintenance_reconcile_catalog_current_ = false;
    }
    return ReconcileStepResult::InProgress;
}

platform::esp::common::storage::StorageOperationResult
SdStore::beginMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation)
{
    if (operation != storage_contracts::StorageOperation::Hydrate &&
        operation != storage_contracts::StorageOperation::Persist &&
        operation != storage_contracts::StorageOperation::Compact)
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::Cancelled,
            operation,
            generation);
    }
    if (operation == storage_contracts::StorageOperation::Hydrate &&
        ready_.load(std::memory_order_acquire))
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }
    if (operation == storage_contracts::StorageOperation::Persist &&
        !persistencePending())
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }
    // A composite adapter may revisit this store after a later store in the
    // same generation asked the owner to retry. Do not reset a completed
    // first stage, especially when the operation is Compaction.
    if (maintenance_.operation == operation &&
        maintenance_.generation == generation &&
        maintenance_.phase == MaintenancePhase::Complete)
    {
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }

    // The persistence lease is the store's logical maintenance ownership, not
    // the physical SD/SPI transaction lease. Keep that ownership across a
    // retry so foreground persistence cannot interleave between generations.
    if (maintenance_.operation == operation &&
        maintenance_.generation == generation &&
        maintenance_.phase != MaintenancePhase::Idle &&
        maintenance_.phase != MaintenancePhase::Complete &&
        maintenance_.phase != MaintenancePhase::Failed)
    {
        if (!maintenance_persistence_locked_ &&
            !acquirePersistenceLease(kPersistenceLeaseWaitTicks))
        {
            return storage_contracts::StorageOperationResult::failure(
                storage_contracts::StorageOperationResultKind::StateBusy,
                operation,
                generation);
        }
        maintenance_persistence_locked_ = true;
        if (operation == storage_contracts::StorageOperation::Hydrate)
        {
            hydrating_.store(true, std::memory_order_release);
        }
        return storage_contracts::StorageOperationResult::inProgressResult(
            operation,
            generation);
    }

    if (!acquirePersistenceLease(kPersistenceLeaseWaitTicks))
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StateBusy,
            operation,
            generation);
    }
    if (maintenance_.phase != MaintenancePhase::Idle &&
        maintenance_.phase != MaintenancePhase::Complete &&
        maintenance_.phase != MaintenancePhase::Failed)
    {
        releasePersistenceLease();
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StateBusy,
            operation,
            generation);
    }

    resetCatalogReconcileCursor();
    maintenance_seen_rebuild_started_ = false;
    maintenance_ = {};
    maintenance_.operation = operation;
    maintenance_.generation = generation;
    maintenance_.phase =
        operation == storage_contracts::StorageOperation::Hydrate
            ? MaintenancePhase::HydrationPrepare
        : operation == storage_contracts::StorageOperation::Persist
            ? MaintenancePhase::PersistenceFlush
            : MaintenancePhase::CompactionPrepare;
    if (operation == storage_contracts::StorageOperation::Hydrate)
    {
        hydrating_.store(true, std::memory_order_release);
    }
    maintenance_persistence_locked_ = true;
    return storage_contracts::StorageOperationResult::inProgressResult(
        operation,
        generation);
}

platform::esp::common::storage::StorageOperationResult
SdStore::stepMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation,
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (maintenance_.operation != operation ||
        maintenance_.generation != generation)
    {
        return storage_contracts::StorageOperationResult::failure(
            storage_contracts::StorageOperationResultKind::StaleGeneration,
            operation,
            generation);
    }
    if (maintenance_.phase == MaintenancePhase::Failed)
    {
        releaseMaintenanceLease();
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::IoError);
    }
    if (maintenance_.phase == MaintenancePhase::Complete)
    {
        releaseMaintenanceLease();
        return storage_contracts::StorageOperationResult::completedResult(
            operation,
            generation);
    }

    const auto result =
        operation == storage_contracts::StorageOperation::Hydrate
            ? stepHydration(budget)
        : operation == storage_contracts::StorageOperation::Persist
            ? stepPersistence(budget)
        : operation == storage_contracts::StorageOperation::Compact
            ? stepCompaction(budget)
            : maintenanceFailure(
                  storage_contracts::StorageOperationResultKind::Cancelled);
    if (result.completed() ||
        result.kind == storage_contracts::StorageOperationResultKind::Cancelled ||
        maintenance_.phase == MaintenancePhase::Failed)
    {
        releaseMaintenanceLease();
    }
    return result;
}

void SdStore::cancelMaintenance(
    platform::esp::common::storage::StorageOperation operation,
    platform::esp::common::storage::StorageOperationGeneration generation)
{
    if (maintenance_.operation != operation ||
        maintenance_.generation != generation)
    {
        return;
    }
    maintenance_journal_.reset();
    resetCatalogReconcileCursor();
    maintenance_seen_rebuild_started_ = false;
    maintenance_.phase = MaintenancePhase::Failed;
    if (operation == storage_contracts::StorageOperation::Hydrate)
    {
        hydrating_.store(false, std::memory_order_release);
    }
    releaseMaintenanceLease();
}

platform::esp::common::storage::StorageOperationResult
SdStore::maintenanceFailure(
    platform::esp::common::storage::StorageOperationResultKind kind) const
{
    return storage_contracts::StorageOperationResult::failure(
        kind,
        maintenance_.operation,
        maintenance_.generation);
}

bool SdStore::prepareMaintenanceJournal()
{
    const MeshProtocol protocol =
        kProtocols[maintenance_.protocol_index];
    const uint8_t index = maintenance_.journal_index;
    const char* name = nullptr;
    storage_v2::JournalKind kind = storage_v2::JournalKind::MessageSegment;
    std::size_t slot_size = 0U;

    switch (index)
    {
    case 0U:
        name = "catalog.snapshot";
        kind = storage_v2::JournalKind::CatalogSnapshot;
        slot_size = storage_v2::catalogSlotSize(protocol);
        break;
    case 1U:
        name = "catalog.delta";
        kind = storage_v2::JournalKind::CatalogDelta;
        slot_size = storage_v2::catalogSlotSize(protocol);
        break;
    case 2U:
        name = "read.snapshot";
        kind = storage_v2::JournalKind::ReadStateSnapshot;
        slot_size = storage_v2::readStateSlotSize(protocol);
        break;
    case 3U:
        name = "read.delta";
        kind = storage_v2::JournalKind::ReadStateDelta;
        slot_size = storage_v2::readStateSlotSize(protocol);
        break;
    case 4U:
        name = "status.snapshot";
        kind = storage_v2::JournalKind::StatusSnapshot;
        slot_size = storage_v2::statusSlotSize();
        break;
    case 5U:
        name = "status.delta";
        kind = storage_v2::JournalKind::StatusDelta;
        slot_size = storage_v2::statusSlotSize();
        break;
    default:
        return false;
    }

    buildProjectionPath(protocol,
                        name,
                        maintenance_path_,
                        sizeof(maintenance_path_));
    maintenance_protocol_ = protocol;
    maintenance_kind_ = kind;
    maintenance_slot_size_ = slot_size;
    maintenance_.journal_started = maintenance_journal_.begin(
        journal_,
        maintenance_path_,
        protocol,
        kind,
        slot_size);
    return maintenance_.journal_started;
}

bool SdStore::applyHydrationJournalSlot(MeshProtocol protocol,
                                        storage_v2::JournalKind kind)
{
    if (maintenance_slot_size_ > maintenance_scratch_.size())
    {
        return false;
    }

    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return false;
    }

    if (kind == storage_v2::JournalKind::CatalogSnapshot ||
        kind == storage_v2::JournalKind::CatalogDelta)
    {
        storage_v2::ChatCatalogProjection projection{};
        if (!storage_v2::decodeCatalogSlot(protocol,
                                           maintenance_scratch_.data(),
                                           maintenance_slot_size_,
                                           projection))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            return true;
        }
        storage_v2::ChatCatalogProjection* existing =
            findCatalog(projection.conversation);
        if (projection.deleted)
        {
            if (existing)
            {
                catalog_.erase(catalog_.begin() +
                               static_cast<std::ptrdiff_t>(existing -
                                                           catalog_.data()));
            }
        }
        else if (existing)
        {
            *existing = projection;
        }
        else
        {
            catalog_.push_back(projection);
        }
        return true;
    }

    if (kind == storage_v2::JournalKind::ReadStateSnapshot ||
        kind == storage_v2::JournalKind::ReadStateDelta)
    {
        storage_v2::ChatReadProjection projection{};
        if (!storage_v2::decodeReadStateSlot(protocol,
                                             maintenance_scratch_.data(),
                                             maintenance_slot_size_,
                                             projection))
        {
            projection_dirty_[protocolIndex(protocol)] = true;
            return true;
        }
        storage_v2::ChatReadProjection* existing =
            findReadState(projection.conversation);
        if (projection.deleted)
        {
            if (existing)
            {
                read_state_.erase(
                    read_state_.begin() +
                    static_cast<std::ptrdiff_t>(existing - read_state_.data()));
            }
        }
        else if (existing)
        {
            *existing = projection;
        }
        else
        {
            read_state_.push_back(projection);
        }
        return true;
    }

    storage_v2::ChatStatusProjection projection{};
    if (!storage_v2::decodeStatusSlot(maintenance_scratch_.data(),
                                      maintenance_slot_size_,
                                      projection))
    {
        projection_dirty_[protocolIndex(protocol)] = true;
        return true;
    }
    storage_v2::ChatStatusProjection* existing =
        findStatus(projection.message_id, protocol);
    if (existing)
    {
        *existing = projection;
    }
    else
    {
        ProtocolStatusProjection state{};
        state.protocol = normalizeProtocol(protocol);
        state.value = projection;
        statuses_.push_back(state);
    }
    return true;
}

bool SdStore::advanceHydrationJournal()
{
    maintenance_journal_.reset();
    maintenance_.journal_started = false;
    ++maintenance_.journal_index;
    if (maintenance_.journal_index < kHydrationJournalCount)
    {
        return true;
    }
    maintenance_.journal_index = 0U;
    ++maintenance_.protocol_index;
    if (maintenance_.protocol_index <
        static_cast<uint8_t>(sizeof(kProtocols) / sizeof(kProtocols[0])))
    {
        maintenance_.phase = MaintenancePhase::HydrationRecover;
        maintenance_.recovery_index = 0U;
    }
    else
    {
        maintenance_.phase = MaintenancePhase::HydrationSeen;
        maintenance_.protocol_index = 0U;
        maintenance_.recovery_index = 0U;
    }
    return true;
}

bool SdStore::recoverHydrationSnapshot()
{
    const char* base_name =
        hydrationRecoveryName(maintenance_.recovery_index);
    if (!base_name)
    {
        maintenance_.phase = MaintenancePhase::HydrationJournal;
        return true;
    }
    const bool ok = recoverProjectionSnapshot(
        kProtocols[maintenance_.protocol_index],
        base_name);
    ++maintenance_.recovery_index;
    if (!ok)
    {
        return false;
    }
    if (maintenance_.recovery_index >= kHydrationRecoveryCount)
    {
        maintenance_.phase = MaintenancePhase::HydrationJournal;
    }
    return true;
}

platform::esp::common::storage::StorageOperationResult
SdStore::stepHydration(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    const uint8_t work_items = std::max<uint8_t>(1U, budget.max_work_items);
    for (uint8_t work = 0U; work < work_items; ++work)
    {
        switch (maintenance_.phase)
        {
        case MaintenancePhase::HydrationPrepare:
            if (!storage_runtime::sd_card_ready() || !ensureLayout() ||
                !resetHydrationState())
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::
                        DeviceUnavailable);
            }
            maintenance_.phase = MaintenancePhase::HydrationRecover;
            break;

        case MaintenancePhase::HydrationRecover:
            if (!recoverHydrationSnapshot())
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            break;

        case MaintenancePhase::HydrationJournal:
            if (!maintenance_.journal_started &&
                !prepareMaintenanceJournal())
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            {
                const auto status = maintenance_journal_.next(
                    journal_,
                    maintenance_scratch_.data(),
                    maintenance_scratch_.size());
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Item)
                {
                    if (!applyHydrationJournalSlot(maintenance_protocol_,
                                                   maintenance_kind_))
                    {
                        return maintenanceFailure(
                            storage_contracts::StorageOperationResultKind::
                                StateBusy);
                    }
                    break;
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Unavailable)
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            RetryLater);
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Invalid)
                {
                    {
                        storage_runtime::ScopedRecursiveStateLock state_lock(
                            mutex_);
                        if (!state_lock.locked())
                        {
                            return maintenanceFailure(
                                stateLockFailure(state_lock.result()));
                        }
                        projection_dirty_[protocolIndex(maintenance_protocol_)] =
                            true;
                    }
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Missing ||
                    status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Complete ||
                    status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Invalid)
                {
                    if (!advanceHydrationJournal())
                    {
                        return maintenanceFailure(
                            storage_contracts::StorageOperationResultKind::
                                IoError);
                    }
                }
            }
            break;

        case MaintenancePhase::HydrationSeen:
            if (maintenance_.recovery_index == 0U)
            {
                if (!recoverProjectionSnapshot(MeshProtocol::Reticulum,
                                               "seen"))
                {
                    maintenance_.phase = MaintenancePhase::Failed;
                    hydrating_.store(false, std::memory_order_release);
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            IoError);
                }
                maintenance_.recovery_index = 1U;
                break;
            }
            if (!maintenance_.journal_started)
            {
                if (maintenance_.journal_index >= 2U)
                {
                    if (!maintenance_.seen_journal_found)
                    {
                        storage_runtime::ScopedRecursiveStateLock lock(mutex_);
                        if (!lock.locked())
                        {
                            return maintenanceFailure(
                                stateLockFailure(lock.result()));
                        }
                        for (const auto& projection : catalog_)
                        {
                            if (!projection.deleted &&
                                sameProtocol(
                                    projection.conversation.protocol,
                                    MeshProtocol::Reticulum) &&
                                projection.message_count > 0U)
                            {
                                maintenance_.seen_rebuild_required = true;
                                break;
                            }
                        }
                    }
                    maintenance_.protocol_index = 0U;
                    maintenance_.phase =
                        maintenance_.seen_rebuild_required
                            ? MaintenancePhase::HydrationRebuildSeen
                            : MaintenancePhase::HydrationReconcile;
                    break;
                }
                const char* name = maintenance_.journal_index == 0U
                                       ? "seen.snapshot"
                                       : "seen.delta";
                buildProjectionPath(MeshProtocol::Reticulum,
                                    name,
                                    maintenance_path_,
                                    sizeof(maintenance_path_));
                maintenance_protocol_ = MeshProtocol::Reticulum;
                maintenance_kind_ = storage_v2::JournalKind::ReticulumSeen;
                maintenance_slot_size_ =
                    storage_v2::reticulumSeenSlotSize();
                if (!maintenance_journal_.begin(
                        journal_,
                        maintenance_path_,
                        maintenance_protocol_,
                        maintenance_kind_,
                        maintenance_slot_size_))
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            IoError);
                }
                const auto& inspection = maintenance_journal_.inspection();
                if (inspection.state ==
                    storage_v2::FixedSlotJournalEngine::State::IoError)
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            RetryLater);
                }
                maintenance_.seen_journal_found |=
                    inspection.state !=
                    storage_v2::FixedSlotJournalEngine::State::Missing;
                maintenance_.seen_rebuild_required |=
                    inspection.state ==
                    storage_v2::FixedSlotJournalEngine::State::PartialTail;
                const uint32_t start =
                    inspection.slot_count > kSeenHotCapacity
                        ? inspection.slot_count -
                              static_cast<uint32_t>(kSeenHotCapacity)
                        : 0U;
                (void)maintenance_journal_.seek(start);
                maintenance_.journal_started = true;
            }
            {
                const auto status = maintenance_journal_.next(
                    journal_,
                    maintenance_scratch_.data(),
                    maintenance_scratch_.size());
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Item)
                {
                    storage_v2::ReticulumSeenProjection projection{};
                    if (!storage_v2::decodeReticulumSeenSlot(
                            maintenance_scratch_.data(),
                            maintenance_slot_size_,
                            projection))
                    {
                        maintenance_.seen_rebuild_required = true;
                        break;
                    }
                    storage_runtime::ScopedRecursiveStateLock lock(mutex_);
                    if (!lock.locked())
                    {
                        return maintenanceFailure(
                            stateLockFailure(lock.result()));
                    }
                    if (seen_hot_.size() == kSeenHotCapacity)
                    {
                        seen_hot_.erase(seen_hot_.begin());
                    }
                    seen_hot_.push_back(projection);
                    break;
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Unavailable)
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::
                            RetryLater);
                }
                if (status == storage_v2::FixedSlotJournalCursor::StepStatus::
                                  Invalid)
                {
                    maintenance_.seen_rebuild_required = true;
                }
                maintenance_journal_.reset();
                maintenance_.journal_started = false;
                ++maintenance_.journal_index;
            }
            break;

        case MaintenancePhase::HydrationRebuildSeen:
        {
            const ReconcileStepResult result = stepSeenRebuild();
            if (result == ReconcileStepResult::Failed)
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            if (result == ReconcileStepResult::Complete)
            {
                maintenance_.protocol_index = 0U;
                maintenance_.phase = MaintenancePhase::HydrationReconcile;
            }
            break;
        }

        case MaintenancePhase::HydrationReconcile:
        {
            if (maintenance_.protocol_index >=
                static_cast<uint8_t>(sizeof(kProtocols) /
                                     sizeof(kProtocols[0])))
            {
                ready_.store(true, std::memory_order_release);
                maintenance_compaction_requested_.store(
                    projection_dirty_[0] || projection_dirty_[1] ||
                        projection_dirty_[2],
                    std::memory_order_release);
                hydrating_.store(false, std::memory_order_release);
                maintenance_.phase = MaintenancePhase::Complete;
                return storage_contracts::StorageOperationResult::
                    completedResult(maintenance_.operation,
                                    maintenance_.generation);
            }
            const ReconcileStepResult result =
                stepProtocolCatalogReconcile(
                    kProtocols[maintenance_.protocol_index]);
            if (result == ReconcileStepResult::Failed)
            {
                maintenance_.phase = MaintenancePhase::Failed;
                hydrating_.store(false, std::memory_order_release);
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            if (result == ReconcileStepResult::Complete)
            {
                ++maintenance_.protocol_index;
            }
            break;
        }

        case MaintenancePhase::Complete:
            return storage_contracts::StorageOperationResult::completedResult(
                maintenance_.operation,
                maintenance_.generation);

        case MaintenancePhase::Failed:
            return maintenanceFailure(
                storage_contracts::StorageOperationResultKind::IoError);

        default:
            return maintenanceFailure(
                storage_contracts::StorageOperationResultKind::StateBusy);
        }
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

platform::esp::common::storage::StorageOperationResult
SdStore::stepPersistence(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (!ready_.load(std::memory_order_acquire) ||
        hydrating_.load(std::memory_order_acquire))
    {
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::StateBusy);
    }

    if (!flushPendingStatusProjections(
            std::max<std::size_t>(1U, budget.max_work_items)))
    {
        return maintenanceFailure(
            storage_runtime::sd_card_ready()
                ? storage_contracts::StorageOperationResultKind::RetryLater
                : storage_contracts::StorageOperationResultKind::
                      DeviceUnavailable);
    }
    if (!persistencePending())
    {
        maintenance_.phase = MaintenancePhase::Complete;
        return storage_contracts::StorageOperationResult::completedResult(
            maintenance_.operation,
            maintenance_.generation);
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

platform::esp::common::storage::StorageOperationResult
SdStore::stepCompaction(
    const platform::esp::common::storage::StorageOperationBudget& budget)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return maintenanceFailure(
            storage_contracts::StorageOperationResultKind::StateBusy);
    }

    const uint8_t work_items = std::max<uint8_t>(1U, budget.max_work_items);
    for (uint8_t work = 0U; work < work_items; ++work)
    {
        switch (maintenance_.phase)
        {
        case MaintenancePhase::CompactionPrepare:
        {
            storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
            if (!state_lock.locked())
            {
                return maintenanceFailure(
                    stateLockFailure(state_lock.result()));
            }
            compaction_catalog_ = catalog_;
            compaction_read_state_ = read_state_;
            compaction_statuses_ = statuses_;
            maintenance_.protocol_index = 0U;
            maintenance_.compaction_projection_index = 0U;
            maintenance_.compaction_inspection_index = 0U;
            maintenance_.compaction_record_index = 0U;
            maintenance_.compact_catalog = false;
            maintenance_.compact_read = false;
            maintenance_.compact_status = false;
            maintenance_.phase = MaintenancePhase::CompactionInspect;
            break;
        }

        case MaintenancePhase::CompactionInspect:
        {
            if (maintenance_.protocol_index >=
                static_cast<uint8_t>(sizeof(kProtocols) /
                                     sizeof(kProtocols[0])))
            {
                maintenance_compaction_requested_.store(
                    false,
                    std::memory_order_release);
                maintenance_.phase = MaintenancePhase::Complete;
                return storage_contracts::StorageOperationResult::
                    completedResult(maintenance_.operation,
                                    maintenance_.generation);
            }

            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const uint8_t index = maintenance_.compaction_inspection_index;
            const char* name = nullptr;
            storage_v2::JournalKind kind =
                storage_v2::JournalKind::CatalogDelta;
            std::size_t slot_size = 0U;
            switch (index)
            {
            case 0U:
                name = "catalog.delta";
                kind = storage_v2::JournalKind::CatalogDelta;
                slot_size = storage_v2::catalogSlotSize(protocol);
                break;
            case 1U:
                name = "read.delta";
                kind = storage_v2::JournalKind::ReadStateDelta;
                slot_size = storage_v2::readStateSlotSize(protocol);
                break;
            case 2U:
                name = "status.delta";
                kind = storage_v2::JournalKind::StatusDelta;
                slot_size = storage_v2::statusSlotSize();
                break;
            default:
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            char path[128] = {};
            buildProjectionPath(protocol, name, path, sizeof(path));
            const auto inspection =
                journal_.inspect(path, protocol, kind, slot_size);
            if (inspection.state ==
                storage_v2::FixedSlotJournalEngine::State::IoError)
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::
                        RetryLater);
            }
            if (inspection.state ==
                storage_v2::FixedSlotJournalEngine::State::Incompatible)
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            bool dirty_catalog = false;
            if (index == 0U)
            {
                storage_runtime::ScopedRecursiveStateLock lock(mutex_);
                if (!lock.locked())
                {
                    return maintenanceFailure(
                        stateLockFailure(lock.result()));
                }
                dirty_catalog = projection_dirty_[protocolIndex(protocol)];
            }
            const bool should_compact = dirty_catalog ||
                                        inspection.slot_count >=
                                            (index == 0U
                                                 ? kCatalogCompactThreshold
                                             : index == 1U
                                                 ? kReadCompactThreshold
                                                 : kStatusCompactThreshold);
            if (index == 0U)
            {
                maintenance_.compact_catalog = should_compact;
            }
            else if (index == 1U)
            {
                maintenance_.compact_read = should_compact;
            }
            else
            {
                maintenance_.compact_status = should_compact;
            }
            ++maintenance_.compaction_inspection_index;
            if (maintenance_.compaction_inspection_index >= 3U)
            {
                maintenance_.compaction_inspection_index = 0U;
                maintenance_.compaction_projection_index = 0U;
                maintenance_.phase = MaintenancePhase::CompactionCreate;
            }
            break;
        }

        case MaintenancePhase::CompactionCreate:
        {
            while (maintenance_.compaction_projection_index < 3U)
            {
                const bool enabled =
                    maintenance_.compaction_projection_index == 0U
                        ? maintenance_.compact_catalog
                    : maintenance_.compaction_projection_index == 1U
                        ? maintenance_.compact_read
                        : maintenance_.compact_status;
                if (enabled)
                {
                    break;
                }
                ++maintenance_.compaction_projection_index;
            }
            if (maintenance_.compaction_projection_index >= 3U)
            {
                maintenance_.phase = MaintenancePhase::CompactionAdvance;
                break;
            }

            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            const uint8_t index = maintenance_.compaction_projection_index;
            const char* base = index == 0U
                                   ? "catalog"
                               : index == 1U ? "read"
                                             : "status";
            const storage_v2::JournalKind snapshot_kind =
                index == 0U
                    ? storage_v2::JournalKind::CatalogSnapshot
                : index == 1U
                    ? storage_v2::JournalKind::ReadStateSnapshot
                    : storage_v2::JournalKind::StatusSnapshot;
            maintenance_slot_size_ =
                index == 0U
                    ? storage_v2::catalogSlotSize(protocol)
                : index == 1U ? storage_v2::readStateSlotSize(protocol)
                              : storage_v2::statusSlotSize();
            maintenance_protocol_ = protocol;
            maintenance_kind_ = snapshot_kind;
            char final_name[40] = {};
            char temp_name[40] = {};
            char backup_name[40] = {};
            char delta_name[40] = {};
            std::snprintf(final_name,
                          sizeof(final_name),
                          "%s.snapshot",
                          base);
            std::snprintf(temp_name,
                          sizeof(temp_name),
                          "%s.snapshot.tmp",
                          base);
            std::snprintf(backup_name,
                          sizeof(backup_name),
                          "%s.snapshot.bak",
                          base);
            std::snprintf(delta_name,
                          sizeof(delta_name),
                          "%s.delta",
                          base);
            buildProjectionPath(protocol,
                                final_name,
                                maintenance_final_path_,
                                sizeof(maintenance_final_path_));
            buildProjectionPath(protocol,
                                temp_name,
                                maintenance_path_,
                                sizeof(maintenance_path_));
            buildProjectionPath(protocol,
                                backup_name,
                                maintenance_backup_path_,
                                sizeof(maintenance_backup_path_));
            buildProjectionPath(protocol,
                                delta_name,
                                maintenance_delta_path_,
                                sizeof(maintenance_delta_path_));
            (void)storage_runtime::sd_remove(maintenance_path_);
            if (!journal_.create(maintenance_path_,
                                 protocol,
                                 snapshot_kind,
                                 maintenance_slot_size_))
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            maintenance_.compaction_record_index = 0U;
            maintenance_.phase = MaintenancePhase::CompactionWrite;
            break;
        }

        case MaintenancePhase::CompactionWrite:
        {
            const uint8_t index = maintenance_.compaction_projection_index;
            const MeshProtocol protocol =
                kProtocols[maintenance_.protocol_index];
            bool has_record = false;
            while (true)
            {
                if (index == 0U)
                {
                    if (maintenance_.compaction_record_index >=
                        compaction_catalog_.size())
                    {
                        break;
                    }
                    const auto& projection =
                        compaction_catalog_[maintenance_.compaction_record_index++];
                    if (!sameProtocol(projection.conversation.protocol,
                                      protocol) ||
                        projection.deleted)
                    {
                        continue;
                    }
                    has_record =
                        storage_v2::encodeCatalogSlot(protocol,
                                                      projection,
                                                      maintenance_scratch_.data(),
                                                      maintenance_slot_size_);
                }
                else if (index == 1U)
                {
                    if (maintenance_.compaction_record_index >=
                        compaction_read_state_.size())
                    {
                        break;
                    }
                    const auto& projection =
                        compaction_read_state_[maintenance_.compaction_record_index++];
                    if (!sameProtocol(projection.conversation.protocol,
                                      protocol) ||
                        projection.deleted)
                    {
                        continue;
                    }
                    has_record =
                        storage_v2::encodeReadStateSlot(protocol,
                                                        projection,
                                                        maintenance_scratch_.data(),
                                                        maintenance_slot_size_);
                }
                else
                {
                    if (maintenance_.compaction_record_index >=
                        compaction_statuses_.size())
                    {
                        break;
                    }
                    const auto& state =
                        compaction_statuses_[maintenance_.compaction_record_index++];
                    if (!sameProtocol(state.protocol, protocol))
                    {
                        continue;
                    }
                    has_record =
                        storage_v2::encodeStatusSlot(
                            state.value,
                            maintenance_scratch_.data(),
                            maintenance_slot_size_);
                }
                if (!has_record ||
                    !journal_.append(maintenance_path_,
                                     protocol,
                                     maintenance_kind_,
                                     maintenance_slot_size_,
                                     maintenance_scratch_.data()))
                {
                    return maintenanceFailure(
                        storage_contracts::StorageOperationResultKind::IoError);
                }
                return storage_contracts::StorageOperationResult::
                    inProgressResult(maintenance_.operation,
                                     maintenance_.generation);
            }
            maintenance_.phase = MaintenancePhase::CompactionReplace;
            break;
        }

        case MaintenancePhase::CompactionReplace:
            if (!storage_v2::replaceFileAtomically(
                    maintenance_path_,
                    maintenance_final_path_,
                    maintenance_backup_path_))
            {
                return maintenanceFailure(
                    storage_contracts::StorageOperationResultKind::IoError);
            }
            maintenance_.phase = MaintenancePhase::CompactionRemove;
            break;

        case MaintenancePhase::CompactionRemove:
            (void)storage_runtime::sd_remove(maintenance_delta_path_);
            if (maintenance_.compaction_projection_index == 0U)
            {
                storage_runtime::ScopedRecursiveStateLock lock(mutex_);
                if (!lock.locked())
                {
                    return maintenanceFailure(
                        stateLockFailure(lock.result()));
                }
                projection_dirty_[protocolIndex(maintenance_protocol_)] =
                    false;
            }
            ++maintenance_.compaction_projection_index;
            maintenance_.phase = MaintenancePhase::CompactionCreate;
            break;

        case MaintenancePhase::CompactionAdvance:
            ++maintenance_.protocol_index;
            if (maintenance_.protocol_index >=
                static_cast<uint8_t>(sizeof(kProtocols) /
                                     sizeof(kProtocols[0])))
            {
                compaction_catalog_.clear();
                compaction_read_state_.clear();
                compaction_statuses_.clear();
                maintenance_compaction_requested_.store(
                    false,
                    std::memory_order_release);
                maintenance_.phase = MaintenancePhase::Complete;
                return storage_contracts::StorageOperationResult::
                    completedResult(maintenance_.operation,
                                    maintenance_.generation);
            }
            maintenance_.compaction_inspection_index = 0U;
            maintenance_.compaction_projection_index = 0U;
            maintenance_.phase = MaintenancePhase::CompactionInspect;
            break;

        case MaintenancePhase::Complete:
            return storage_contracts::StorageOperationResult::completedResult(
                maintenance_.operation,
                maintenance_.generation);

        default:
            return maintenanceFailure(
                storage_contracts::StorageOperationResultKind::StateBusy);
        }
    }
    return storage_contracts::StorageOperationResult::inProgressResult(
        maintenance_.operation,
        maintenance_.generation);
}

void SdStore::append(const ChatMessage& msg)
{
    if (!appendDurably(msg))
    {
        CHAT_STORE_LOG("[ChatStoreV2] append deferred protocol=%s msg=%08lX\n",
                       protocolSlug(msg.protocol),
                       static_cast<unsigned long>(msg.msg_id));
    }
}

bool SdStore::appendDurably(const ChatMessage& msg)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    return appendInternal(msg, false);
}

bool SdStore::appendIncomingDurably(const ChatMessage& msg)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    return appendInternal(msg, true);
}

bool SdStore::appendInternal(const ChatMessage& input, bool incoming_commit)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked())
    {
        return false;
    }
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }

    ChatMessage message = input;
    message.protocol = normalizeProtocol(message.protocol);
    if (!storage_v2::supportedProtocol(message.protocol) ||
        message.text.size() >
            (message.protocol == MeshProtocol::Meshtastic
                 ? storage_v2::kMeshtasticTextMax
             : message.protocol == MeshProtocol::MeshCore
                 ? storage_v2::kMeshCoreTextMax
                 : storage_v2::kReticulumTextMax) ||
        !ensureProtocolLayout(message.protocol))
    {
        return false;
    }

    const ConversationId conversation = conversationIdForMessage(message);
    bool already_stored = false;
    uint32_t stored_count = 0;
    if (!storedMessageMatches(message, &already_stored, &stored_count))
    {
        return false;
    }

    if (!already_stored)
    {
        const uint32_t sequence = stored_count + 1U;
        if (!appendMessageRecord(message, sequence))
        {
            return false;
        }
        stored_count = sequence;
    }

    if (incoming_commit && chat::hasReticulumLxmfMessageHash(message) &&
        !rememberReticulumHash(message.reticulum_lxmf_hash))
    {
        // The message record is authoritative and retry detection above is
        // idempotent. Do not publish until its Reticulum dedup identity is also
        // durable.
        return false;
    }

    storage_v2::ChatCatalogProjection projection_snapshot{};
    uint32_t last_read_sequence = 0U;
    bool projection_was_current = false;
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        if (const storage_v2::ChatCatalogProjection* projection =
                findCatalog(conversation))
        {
            projection_snapshot = *projection;
            projection_was_current =
                projection->message_count == stored_count &&
                projection->last_message_id == message.msg_id;
        }
        else
        {
            projection_snapshot.conversation = conversation;
        }
        if (const storage_v2::ChatReadProjection* read_state =
                findReadState(conversation))
        {
            last_read_sequence = read_state->last_read_sequence;
        }
    }

    bool projection_persisted = true;
    if (!projection_was_current)
    {
        projection_snapshot.conversation = conversation;
        projection_snapshot.message_count = stored_count;
        projection_snapshot.last_sequence = stored_count;
        projection_snapshot.last_message_id = message.msg_id;
        projection_snapshot.last_timestamp = message.timestamp;
        projection_snapshot.last_status = message.status;
        projection_snapshot.deleted = false;
        projection_snapshot.unread =
            countUnreadAfter(conversation, last_read_sequence);
        copyTextPreview(projection_snapshot.preview,
                        sizeof(projection_snapshot.preview),
                        message.text);
        projection_persisted =
            appendCatalogProjection(projection_snapshot);

        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        if (storage_v2::ChatCatalogProjection* projection =
                findCatalog(conversation))
        {
            *projection = projection_snapshot;
        }
        else
        {
            catalog_.push_back(projection_snapshot);
        }
        if (!projection_persisted)
        {
            projection_dirty_[protocolIndex(message.protocol)] = true;
        }
    }

    if (!projection_persisted)
    {
        CHAT_STORE_LOG("[ChatStoreV2] projection deferred protocol=%s msg=%08lX authoritative=1\n",
                       protocolSlug(message.protocol),
                       static_cast<unsigned long>(message.msg_id));
    }
    CHAT_STORE_LOG("[ChatStoreV2] commit protocol=%s msg=%08lX seq=%lu duplicate=%u publish=1\n",
                   protocolSlug(message.protocol),
                   static_cast<unsigned long>(message.msg_id),
                   static_cast<unsigned long>(stored_count),
                   already_stored ? 1U : 0U);
    return true;
}

std::vector<ChatMessage> SdStore::loadRecent(const ConversationId& conv,
                                             std::size_t n)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return {};
    }
    return loadPageFromLatest(conv, 0, n, nullptr);
}

std::vector<ChatMessage> SdStore::loadPageFromLatest(
    const ConversationId& input,
    std::size_t offset_from_latest,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked() ||
        !ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const uint32_t count = messageCountOnDisk(conversation);
    if (total)
    {
        *total = count;
    }

    std::vector<ChatMessage> result;
    if (limit == 0U || offset_from_latest >= count)
    {
        return result;
    }
    const uint32_t end = count - static_cast<uint32_t>(offset_from_latest);
    const uint32_t start =
        end > limit ? end - static_cast<uint32_t>(limit) : 0U;
    result.reserve(end - start);
    for (uint32_t ordinal = start; ordinal < end; ++ordinal)
    {
        ChatMessage message{};
        if (readMessageByOrdinal(conversation, ordinal, message))
        {
            result.push_back(std::move(message));
        }
    }
    return result;
}

std::vector<ConversationMeta> SdStore::loadConversationPage(
    std::size_t offset,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    std::vector<ConversationMeta> result;
    result.reserve(catalog_.size());
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (!projection.deleted && projection.message_count != 0U)
        {
            result.push_back(makeMeta(projection));
        }
    }
    std::stable_sort(result.begin(),
                     result.end(),
                     [](const ConversationMeta& lhs,
                        const ConversationMeta& rhs)
                     {
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
    if (total)
    {
        *total = result.size();
    }
    if (offset >= result.size())
    {
        return {};
    }
    const std::size_t end =
        limit == 0U ? result.size()
                    : std::min(result.size(), offset + limit);
    return std::vector<ConversationMeta>(
        result.begin() + static_cast<std::ptrdiff_t>(offset),
        result.begin() + static_cast<std::ptrdiff_t>(end));
}

std::vector<ConversationMeta> SdStore::loadConversationPageForProtocol(
    MeshProtocol protocol,
    std::size_t offset,
    std::size_t limit,
    std::size_t* total)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        if (total)
        {
            *total = 0U;
        }
        return {};
    }
    protocol = normalizeProtocol(protocol);
    std::vector<ConversationMeta> result;
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (!projection.deleted && projection.message_count != 0U &&
            sameProtocol(projection.conversation.protocol, protocol))
        {
            result.push_back(makeMeta(projection));
        }
    }
    std::stable_sort(result.begin(),
                     result.end(),
                     [](const ConversationMeta& lhs,
                        const ConversationMeta& rhs)
                     {
                         return lhs.last_timestamp > rhs.last_timestamp;
                     });
    if (total)
    {
        *total = result.size();
    }
    if (offset >= result.size())
    {
        return {};
    }
    const std::size_t end =
        limit == 0U ? result.size()
                    : std::min(result.size(), offset + limit);
    return std::vector<ConversationMeta>(
        result.begin() + static_cast<std::ptrdiff_t>(offset),
        result.begin() + static_cast<std::ptrdiff_t>(end));
}

bool SdStore::setUnread(const ConversationId& input, int unread)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked())
    {
        return false;
    }
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    storage_v2::ChatCatalogProjection catalog_snapshot{};
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        const storage_v2::ChatCatalogProjection* catalog =
            findCatalog(conversation);
        if (!catalog)
        {
            return unread == 0;
        }
        catalog_snapshot = *catalog;
    }

    const uint32_t bounded_unread =
        unread <= 0 ? 0U : static_cast<uint32_t>(unread);
    storage_v2::ChatReadProjection projection{};
    projection.conversation = conversation;
    projection.last_read_sequence =
        sequenceForUnread(conversation, bounded_unread);
    if (!appendReadProjection(projection))
    {
        return false;
    }
    catalog_snapshot.unread =
        countUnreadAfter(conversation,
                         projection.last_read_sequence);
    const bool catalog_persisted =
        appendCatalogProjection(catalog_snapshot);

    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    if (storage_v2::ChatReadProjection* current =
            findReadState(conversation))
    {
        *current = projection;
    }
    else
    {
        read_state_.push_back(projection);
    }
    if (storage_v2::ChatCatalogProjection* catalog =
            findCatalog(conversation))
    {
        *catalog = catalog_snapshot;
    }
    else
    {
        catalog_.push_back(catalog_snapshot);
    }
    if (!catalog_persisted)
    {
        projection_dirty_[protocolIndex(conversation.protocol)] = true;
    }
    return true;
}

int SdStore::getUnread(const ConversationId& input) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return 0;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return 0;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const storage_v2::ChatCatalogProjection* projection =
        findCatalog(conversation);
    return projection ? static_cast<int>(projection->unread) : 0;
}

void SdStore::clearConversation(const ConversationId& input)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked())
    {
        return;
    }
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    storage_v2::ChatCatalogProjection tombstone{};
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return;
        }
        if (const storage_v2::ChatCatalogProjection* existing =
                findCatalog(conversation))
        {
            tombstone = *existing;
        }
    }

    char path[128]{};
    buildConversationDirectory(conversation, path, sizeof(path));
    if (!removeTree(path))
    {
        return;
    }

    tombstone.conversation = conversation;
    tombstone.deleted = true;
    const bool catalog_persisted =
        appendCatalogProjection(tombstone);

    storage_v2::ChatReadProjection read_tombstone{};
    read_tombstone.conversation = conversation;
    read_tombstone.deleted = true;
    const bool read_persisted =
        appendReadProjection(read_tombstone);

    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return;
    }
    catalog_.erase(std::remove_if(catalog_.begin(),
                                  catalog_.end(),
                                  [&](const auto& value)
                                  {
                                      return sameConversationKey(
                                          value.conversation,
                                          conversation);
                                  }),
                   catalog_.end());
    read_state_.erase(std::remove_if(read_state_.begin(),
                                     read_state_.end(),
                                     [&](const auto& value)
                                     {
                                         return sameConversationKey(
                                             value.conversation,
                                             conversation);
                                     }),
                      read_state_.end());
    if (!catalog_persisted || !read_persisted)
    {
        projection_dirty_[protocolIndex(conversation.protocol)] = true;
    }
}

void SdStore::clearAll()
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked())
    {
        return;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        (void)removeTree(protocolRoot(protocol));
    }
    const bool layout_ready = ensureLayout();

    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return;
    }
    catalog_.clear();
    read_state_.clear();
    statuses_.clear();
    pending_status_projections_.clear();
    pending_status_head_ = 0U;
    pending_status_revision_ = 0U;
    flush_status_batch_.clear();
    refreshPersistenceDemandLocked();
    seen_hot_.clear();
    std::memset(projection_dirty_, 0, sizeof(projection_dirty_));
    ready_.store(layout_ready, std::memory_order_release);
    maintenance_compaction_requested_.store(
        false,
        std::memory_order_release);
}

bool SdStore::updateMessageStatus(MessageId msg_id, MessageStatus status)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked())
    {
        return false;
    }
    ChatMessage message{};
    if (!getMessage(msg_id, &message))
    {
        return false;
    }
    return updateMessageStatusForProtocol(msg_id, message.protocol, status);
}

bool SdStore::updateMessageStatusForProtocol(MessageId msg_id,
                                             MeshProtocol protocol,
                                             MessageStatus status)
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    protocol = normalizeProtocol(protocol);
    if (msg_id == 0U || !storage_v2::supportedProtocol(protocol))
    {
        return false;
    }

    storage_v2::ChatStatusProjection projection{};
    projection.message_id = msg_id;
    projection.status = status;
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        if (const storage_v2::ChatStatusProjection* current =
                findStatus(msg_id, protocol))
        {
            projection.sequence = current->sequence + 1U;
        }
        else
        {
            projection.sequence = 1U;
        }
        ProtocolStatusProjection queued{};
        queued.protocol = protocol;
        queued.value = projection;
        if (!queueStatusProjectionLocked(queued))
        {
            return false;
        }
        if (storage_v2::ChatStatusProjection* current =
                findStatus(msg_id, protocol))
        {
            *current = projection;
        }
        else
        {
            ProtocolStatusProjection state{};
            state.protocol = protocol;
            state.value = projection;
            statuses_.push_back(state);
        }
        for (storage_v2::ChatCatalogProjection& catalog : catalog_)
        {
            if (!catalog.deleted &&
                sameProtocol(catalog.conversation.protocol, protocol) &&
                catalog.last_message_id == msg_id)
            {
                catalog.last_status = status;
                break;
            }
        }
    }
    return true;
}

bool SdStore::getMessage(MessageId msg_id, ChatMessage* out) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked() ||
        !ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        if (getMessageForProtocol(msg_id, protocol, out))
        {
            return true;
        }
    }
    return false;
}

bool SdStore::getMessageForProtocol(MessageId msg_id,
                                    MeshProtocol protocol,
                                    ChatMessage* out) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked() ||
        !ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    protocol = normalizeProtocol(protocol);
    if (msg_id == 0U)
    {
        return false;
    }

    CatalogList catalog_snapshot{};
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        catalog_snapshot.reserve(catalog_.size());
        for (const storage_v2::ChatCatalogProjection& projection : catalog_)
        {
            if (!projection.deleted &&
                sameProtocol(projection.conversation.protocol, protocol))
            {
                catalog_snapshot.push_back(projection);
            }
        }
    }

    for (const storage_v2::ChatCatalogProjection& projection :
         catalog_snapshot)
    {
        for (uint32_t ordinal = projection.message_count; ordinal > 0U;
             --ordinal)
        {
            ChatMessage message{};
            if (!readMessageByOrdinal(projection.conversation,
                                      ordinal - 1U,
                                      message) ||
                message.msg_id != msg_id)
            {
                continue;
            }
            if (out)
            {
                *out = std::move(message);
            }
            return true;
        }
    }
    return false;
}

bool SdStore::hasReticulumLxmfMessageHash(const uint8_t* hash) const
{
    if (!ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    if (!hash || isAllZeroKeyBytes(hash, kReticulumLxmfHashSize))
    {
        return false;
    }
    ScopedPersistenceLease persistence_lease(persistence_mutex_,
                                             kPersistenceLeaseWaitTicks);
    if (!persistence_lease.locked() ||
        !ready_.load(std::memory_order_acquire))
    {
        return false;
    }
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        for (const storage_v2::ReticulumSeenProjection& seen : seen_hot_)
        {
            if (std::memcmp(seen.hash, hash, sizeof(seen.hash)) == 0)
            {
                return true;
            }
        }
    }

    for (const char* name : {"seen.delta", "seen.snapshot"})
    {
        char path[128]{};
        buildProjectionPath(MeshProtocol::Reticulum,
                            name,
                            path,
                            sizeof(path));
        const auto inspection = journal_.inspect(
            path,
            MeshProtocol::Reticulum,
            storage_v2::JournalKind::ReticulumSeen,
            storage_v2::reticulumSeenSlotSize());
        for (uint32_t index = inspection.slot_count; index > 0U; --index)
        {
            if (!journal_.read(path,
                               MeshProtocol::Reticulum,
                               storage_v2::JournalKind::ReticulumSeen,
                               storage_v2::reticulumSeenSlotSize(),
                               index - 1U,
                               scratch_.data()))
            {
                continue;
            }
            storage_v2::ReticulumSeenProjection projection{};
            if (storage_v2::decodeReticulumSeenSlot(
                    scratch_.data(),
                    storage_v2::reticulumSeenSlotSize(),
                    projection) &&
                std::memcmp(projection.hash, hash, sizeof(projection.hash)) == 0)
            {
                return true;
            }
        }
    }
    return false;
}

void SdStore::flush()
{
    // Record appends are already durable. Projection compaction belongs to
    // StorageMaintenanceOwner, so a foreground flush never starts filesystem
    // work or competes with the logical state lock.
}

bool SdStore::ensureLayout() const
{
    if (!storage_runtime::sd_card_ready() || !ensureDirectory("/data") ||
        !ensureDirectory(kRoot))
    {
        return false;
    }
    for (MeshProtocol protocol : kProtocols)
    {
        if (!ensureProtocolLayout(protocol))
        {
            return false;
        }
    }
    return true;
}

bool SdStore::ensureProtocolLayout(MeshProtocol protocol) const
{
    protocol = normalizeProtocol(protocol);
    const char* slug = protocolSlug(protocol);
    char protocol_dir[64]{};
    char conversations[96]{};
    std::snprintf(protocol_dir,
                  sizeof(protocol_dir),
                  "%s/%s",
                  kRoot,
                  slug);
    std::snprintf(conversations,
                  sizeof(conversations),
                  "%s/conversations",
                  protocolRoot(protocol));
    return ensureDirectory(protocol_dir) &&
           ensureDirectory(protocolRoot(protocol)) &&
           ensureDirectory(conversations);
}

bool SdStore::recoverProjectionSnapshot(MeshProtocol protocol,
                                        const char* base_name)
{
    if (!base_name || base_name[0] == '\0')
    {
        return false;
    }
    char final_path[128] = {};
    char temp_path[128] = {};
    char backup_path[128] = {};
    char name[40] = {};
    std::snprintf(name, sizeof(name), "%s.snapshot", base_name);
    buildProjectionPath(protocol, name, final_path, sizeof(final_path));
    std::snprintf(name, sizeof(name), "%s.snapshot.tmp", base_name);
    buildProjectionPath(protocol, name, temp_path, sizeof(temp_path));
    std::snprintf(name, sizeof(name), "%s.snapshot.bak", base_name);
    buildProjectionPath(protocol, name, backup_path, sizeof(backup_path));
    return storage_v2::recoverAtomicFile(final_path,
                                         temp_path,
                                         backup_path);
}

SdStore::ReconcileStepResult
SdStore::stepConversationDirectoryReconcile(
    MeshProtocol protocol,
    const char* directory_name)
{
    protocol = normalizeProtocol(protocol);
    if (!directory_name || directory_name[0] == '\0')
    {
        return ReconcileStepResult::Failed;
    }

    const std::size_t slot_size = storage_v2::messageSlotSize(protocol);
    if (slot_size == 0U || slot_size > scratch_.size())
    {
        return ReconcileStepResult::Failed;
    }

    switch (maintenance_reconcile_phase_)
    {
    case ConversationReconcilePhase::ScanSegments:
    {
        if (maintenance_reconcile_segment_ >= 10000U)
        {
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::ReadLatest;
            return ReconcileStepResult::InProgress;
        }

        char path[160]{};
        std::snprintf(path,
                      sizeof(path),
                      "%s/%04lu.msg",
                      maintenance_reconcile_directory_path_,
                      static_cast<unsigned long>(
                          maintenance_reconcile_segment_));
        const auto inspection = journal_.inspect(
            path,
            protocol,
            storage_v2::JournalKind::MessageSegment,
            slot_size);
        if (inspection.state ==
            storage_v2::FixedSlotJournalEngine::State::Missing)
        {
            if (!maintenance_reconcile_found_segment_)
            {
                return ReconcileStepResult::Complete;
            }
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::ReadLatest;
            return ReconcileStepResult::InProgress;
        }
        if (inspection.state ==
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::RepairSegment;
            return ReconcileStepResult::InProgress;
        }
        if (inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::Ready)
        {
            return ReconcileStepResult::Failed;
        }

        maintenance_reconcile_found_segment_ = true;
        maintenance_reconcile_total_count_ += inspection.slot_count;
        maintenance_reconcile_last_segment_ =
            maintenance_reconcile_segment_;
        maintenance_reconcile_last_segment_count_ =
            inspection.slot_count;
        ++maintenance_reconcile_segment_;
        if (inspection.slot_count < slotsPerMessageSegment(protocol))
        {
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::ReadLatest;
        }
        return ReconcileStepResult::InProgress;
    }

    case ConversationReconcilePhase::RepairSegment:
    {
        char path[160]{};
        std::snprintf(path,
                      sizeof(path),
                      "%s/%04lu.msg",
                      maintenance_reconcile_directory_path_,
                      static_cast<unsigned long>(
                          maintenance_reconcile_segment_));
        if (!repairPartialJournal(
                path,
                protocol,
                storage_v2::JournalKind::MessageSegment,
                slot_size))
        {
            return ReconcileStepResult::Failed;
        }
        maintenance_reconcile_phase_ =
            ConversationReconcilePhase::ScanSegments;
        return ReconcileStepResult::InProgress;
    }

    case ConversationReconcilePhase::ReadLatest:
    {
        if (!maintenance_reconcile_found_segment_ ||
            maintenance_reconcile_total_count_ == 0U ||
            maintenance_reconcile_last_segment_count_ == 0U)
        {
            return ReconcileStepResult::Complete;
        }

        char last_path[160]{};
        std::snprintf(
            last_path,
            sizeof(last_path),
            "%s/%04lu.msg",
            maintenance_reconcile_directory_path_,
            static_cast<unsigned long>(
                maintenance_reconcile_last_segment_));
        if (!journal_.read(
                last_path,
                protocol,
                storage_v2::JournalKind::MessageSegment,
                slot_size,
                maintenance_reconcile_last_segment_count_ - 1U,
                scratch_.data()))
        {
            return ReconcileStepResult::Failed;
        }
        uint32_t sequence = 0U;
        if (!storage_v2::decodeMessageSlot(
                protocol,
                scratch_.data(),
                slot_size,
                maintenance_reconcile_latest_message_,
                &sequence))
        {
            return ReconcileStepResult::Failed;
        }

        ConversationId conversation{};
        uint32_t last_read = 0U;
        {
            storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
            if (!state_lock.locked())
            {
                return ReconcileStepResult::InProgress;
            }
            ChatMessage& latest =
                maintenance_reconcile_latest_message_;
            applyStoredStatus(latest);
            conversation = conversationIdForMessage(latest);
            if (const storage_v2::ChatCatalogProjection* projection =
                    findCatalog(conversation))
            {
                maintenance_reconcile_projection_ = *projection;
                maintenance_reconcile_catalog_current_ =
                    projection->message_count ==
                        maintenance_reconcile_total_count_ &&
                    projection->last_message_id == latest.msg_id &&
                    projection->last_sequence == sequence;
            }
            else
            {
                maintenance_reconcile_projection_ = {};
                maintenance_reconcile_catalog_current_ = false;
            }
            if (const storage_v2::ChatReadProjection* read_state =
                    findReadState(conversation))
            {
                last_read = read_state->last_read_sequence;
            }
        }

        ChatMessage& latest = maintenance_reconcile_latest_message_;
        storage_v2::ChatCatalogProjection& projection =
            maintenance_reconcile_projection_;
        projection.conversation = conversation;
        projection.message_count =
            maintenance_reconcile_total_count_;
        projection.last_sequence = sequence;
        projection.last_message_id = latest.msg_id;
        projection.last_timestamp = latest.timestamp;
        projection.last_status = latest.status;
        projection.deleted = false;
        copyTextPreview(projection.preview,
                        sizeof(projection.preview),
                        latest.text);

        maintenance_reconcile_unread_ordinal_ = last_read;
        maintenance_reconcile_unread_count_ = 0U;
        if (maintenance_reconcile_catalog_current_)
        {
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::Commit;
        }
        else
        {
            projection.unread = 0U;
            maintenance_reconcile_phase_ =
                last_read < maintenance_reconcile_total_count_
                    ? ConversationReconcilePhase::ScanUnread
                    : ConversationReconcilePhase::Commit;
        }
        return ReconcileStepResult::InProgress;
    }

    case ConversationReconcilePhase::ScanUnread:
    {
        if (maintenance_reconcile_unread_ordinal_ >=
            maintenance_reconcile_total_count_)
        {
            maintenance_reconcile_projection_.unread =
                maintenance_reconcile_unread_count_;
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::Commit;
            return ReconcileStepResult::InProgress;
        }

        uint32_t sequence = 0U;
        if (!readMessageByOrdinal(
                maintenance_reconcile_projection_.conversation,
                maintenance_reconcile_unread_ordinal_,
                maintenance_reconcile_latest_message_,
                &sequence))
        {
            return ReconcileStepResult::Failed;
        }
        if (sequence > maintenance_reconcile_unread_ordinal_ &&
            maintenance_reconcile_latest_message_.status ==
                MessageStatus::Incoming)
        {
            ++maintenance_reconcile_unread_count_;
        }
        ++maintenance_reconcile_unread_ordinal_;
        if (maintenance_reconcile_unread_ordinal_ >=
            maintenance_reconcile_total_count_)
        {
            maintenance_reconcile_projection_.unread =
                maintenance_reconcile_unread_count_;
            maintenance_reconcile_phase_ =
                ConversationReconcilePhase::Commit;
        }
        return ReconcileStepResult::InProgress;
    }

    case ConversationReconcilePhase::Commit:
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked())
        {
            return ReconcileStepResult::InProgress;
        }
        if (storage_v2::ChatCatalogProjection* projection =
                findCatalog(
                    maintenance_reconcile_projection_.conversation))
        {
            *projection = maintenance_reconcile_projection_;
        }
        else
        {
            catalog_.push_back(maintenance_reconcile_projection_);
        }
        if (!maintenance_reconcile_catalog_current_)
        {
            projection_dirty_[protocolIndex(protocol)] = true;
        }
        return ReconcileStepResult::Complete;
    }
    }
    return ReconcileStepResult::Failed;
}

std::size_t SdStore::slotsPerMessageSegment(MeshProtocol protocol) const
{
    const std::size_t slot_size = storage_v2::messageSlotSize(protocol);
    if (slot_size == 0U || kMessageSegmentBytes <=
                               storage_v2::FixedSlotJournalEngine::headerSize())
    {
        return 0U;
    }
    return (kMessageSegmentBytes -
            storage_v2::FixedSlotJournalEngine::headerSize()) /
           slot_size;
}

uint32_t SdStore::messageCountOnDisk(const ConversationId& conversation) const
{
    const std::size_t slot_size =
        storage_v2::messageSlotSize(conversation.protocol);
    uint32_t total_count = 0;
    for (uint32_t segment = 0; segment < 10000U; ++segment)
    {
        char path[128]{};
        buildMessageSegmentPath(conversation, segment, path, sizeof(path));
        const auto inspection = journal_.inspect(
            path,
            conversation.protocol,
            storage_v2::JournalKind::MessageSegment,
            slot_size);
        if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
        {
            break;
        }
        if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
            inspection.state !=
                storage_v2::FixedSlotJournalEngine::State::PartialTail)
        {
            break;
        }
        total_count += inspection.slot_count;
        if (inspection.slot_count < slotsPerMessageSegment(conversation.protocol))
        {
            break;
        }
    }
    return total_count;
}

bool SdStore::readMessageByOrdinal(const ConversationId& input,
                                   uint32_t ordinal,
                                   ChatMessage& out_message,
                                   uint32_t* out_sequence) const
{
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    const std::size_t capacity = slotsPerMessageSegment(conversation.protocol);
    const std::size_t slot_size =
        storage_v2::messageSlotSize(conversation.protocol);
    if (capacity == 0U || slot_size > scratch_.size())
    {
        return false;
    }
    const uint32_t segment = static_cast<uint32_t>(ordinal / capacity);
    const uint32_t slot = static_cast<uint32_t>(ordinal % capacity);
    char path[128]{};
    buildMessageSegmentPath(conversation, segment, path, sizeof(path));
    if (!journal_.read(path,
                       conversation.protocol,
                       storage_v2::JournalKind::MessageSegment,
                       slot_size,
                       slot,
                       scratch_.data()) ||
        !storage_v2::decodeMessageSlot(conversation.protocol,
                                       scratch_.data(),
                                       slot_size,
                                       out_message,
                                       out_sequence))
    {
        return false;
    }
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked())
        {
            return false;
        }
        applyStoredStatus(out_message);
    }
    return true;
}

bool SdStore::latestStoredMessage(const ConversationId& conversation,
                                  ChatMessage& out_message,
                                  uint32_t* out_count) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    if (out_count)
    {
        *out_count = count;
    }
    return count != 0U &&
           readMessageByOrdinal(conversation, count - 1U, out_message);
}

bool SdStore::storedMessageMatches(const ChatMessage& message,
                                   bool* out_matches,
                                   uint32_t* out_count) const
{
    if (!out_matches || !out_count)
    {
        return false;
    }
    *out_matches = false;
    *out_count = 0U;
    const ConversationId conversation = conversationIdForMessage(message);
    ChatMessage latest{};
    if (!latestStoredMessage(conversation, latest, out_count))
    {
        return *out_count == 0U;
    }
    *out_matches = sameStoredMessage(latest, message);
    return true;
}

bool SdStore::appendMessageRecord(const ChatMessage& message,
                                  uint32_t sequence)
{
    const ConversationId conversation = conversationIdForMessage(message);
    char directory[128]{};
    buildConversationDirectory(conversation, directory, sizeof(directory));
    if (!ensureDirectory(directory))
    {
        return false;
    }
    const std::size_t capacity = slotsPerMessageSegment(message.protocol);
    const std::size_t slot_size = storage_v2::messageSlotSize(message.protocol);
    if (capacity == 0U || slot_size > scratch_.size() || sequence == 0U ||
        !storage_v2::encodeMessageSlot(message,
                                       sequence,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    const uint32_t ordinal = sequence - 1U;
    const uint32_t segment = static_cast<uint32_t>(ordinal / capacity);
    const uint32_t expected_slot = static_cast<uint32_t>(ordinal % capacity);
    char path[128]{};
    buildMessageSegmentPath(conversation, segment, path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       message.protocol,
                                       storage_v2::JournalKind::MessageSegment,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        if (!repairPartialJournal(path,
                                  message.protocol,
                                  storage_v2::JournalKind::MessageSegment,
                                  slot_size))
        {
            return false;
        }
        inspection = journal_.inspect(path,
                                      message.protocol,
                                      storage_v2::JournalKind::MessageSegment,
                                      slot_size);
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Missing &&
        inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready)
    {
        return false;
    }
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.slot_count != expected_slot)
    {
        return false;
    }
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing &&
        expected_slot != 0U)
    {
        return false;
    }
    // Tail recovery reuses scratch_, so restore the command record before the
    // authoritative append.
    if (!storage_v2::encodeMessageSlot(message,
                                       sequence,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    return journal_.append(path,
                           message.protocol,
                           storage_v2::JournalKind::MessageSegment,
                           slot_size,
                           scratch_.data());
}

bool SdStore::repairPartialJournal(const char* path,
                                   MeshProtocol protocol,
                                   storage_v2::JournalKind kind,
                                   std::size_t slot_size) const
{
    const auto inspection = journal_.inspect(path, protocol, kind, slot_size);
    if (inspection.state !=
        storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        return inspection.state ==
               storage_v2::FixedSlotJournalEngine::State::Ready;
    }
    char temp[144]{};
    char backup[144]{};
    std::snprintf(temp, sizeof(temp), "%s.repair", path);
    std::snprintf(backup, sizeof(backup), "%s.partial", path);
    (void)storage_runtime::sd_remove(temp);
    (void)storage_runtime::sd_remove(backup);
    if (!journal_.create(temp, protocol, kind, slot_size))
    {
        return false;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        if (!journal_.read(path,
                           protocol,
                           kind,
                           slot_size,
                           index,
                           scratch_.data()) ||
            !journal_.append(temp,
                             protocol,
                             kind,
                             slot_size,
                             scratch_.data()))
        {
            (void)storage_runtime::sd_remove(temp);
            return false;
        }
    }
    if (!storage_runtime::sd_rename(path, backup) ||
        !storage_runtime::sd_rename(temp, path))
    {
        if (!storage_runtime::sd_exists(path))
        {
            (void)storage_runtime::sd_rename(backup, path);
        }
        (void)storage_runtime::sd_remove(temp);
        return false;
    }
    (void)storage_runtime::sd_remove(backup);
    return true;
}

bool SdStore::appendCatalogProjection(
    const storage_v2::ChatCatalogProjection& projection)
{
    const MeshProtocol protocol =
        normalizeProtocol(projection.conversation.protocol);
    const std::size_t slot_size = storage_v2::catalogSlotSize(protocol);
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeCatalogSlot(protocol,
                                       projection,
                                       scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "catalog.delta", path, sizeof(path));
    const bool ok = journal_.append(path,
                                    protocol,
                                    storage_v2::JournalKind::CatalogDelta,
                                    slot_size,
                                    scratch_.data());
    if (ok)
    {
        maintenance_compaction_requested_.store(
            true,
            std::memory_order_release);
    }
    return ok;
}

bool SdStore::appendReadProjection(
    const storage_v2::ChatReadProjection& projection)
{
    const MeshProtocol protocol =
        normalizeProtocol(projection.conversation.protocol);
    const std::size_t slot_size = storage_v2::readStateSlotSize(protocol);
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeReadStateSlot(protocol,
                                         projection,
                                         scratch_.data(),
                                         slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "read.delta", path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       protocol,
                                       storage_v2::JournalKind::ReadStateDelta,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              protocol,
                              storage_v2::JournalKind::ReadStateDelta,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeReadStateSlot(protocol,
                                         projection,
                                         scratch_.data(),
                                         slot_size))
    {
        return false;
    }
    const bool ok = journal_.append(path,
                                    protocol,
                                    storage_v2::JournalKind::ReadStateDelta,
                                    slot_size,
                                    scratch_.data());
    if (ok)
    {
        maintenance_compaction_requested_.store(
            true,
            std::memory_order_release);
    }
    return ok;
}

bool SdStore::appendStatusProjection(
    MeshProtocol protocol,
    const storage_v2::ChatStatusProjection& projection)
{
    protocol = normalizeProtocol(protocol);
    const std::size_t slot_size = storage_v2::statusSlotSize();
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeStatusSlot(projection,
                                      scratch_.data(),
                                      slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(protocol, "status.delta", path, sizeof(path));
    auto inspection = journal_.inspect(path,
                                       protocol,
                                       storage_v2::JournalKind::StatusDelta,
                                       slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              protocol,
                              storage_v2::JournalKind::StatusDelta,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeStatusSlot(projection,
                                      scratch_.data(),
                                      slot_size))
    {
        return false;
    }
    const bool ok = journal_.append(path,
                                    protocol,
                                    storage_v2::JournalKind::StatusDelta,
                                    slot_size,
                                    scratch_.data());
    if (ok)
    {
        maintenance_compaction_requested_.store(
            true,
            std::memory_order_release);
    }
    return ok;
}

bool SdStore::queueStatusProjectionLocked(
    const ProtocolStatusProjection& projection)
{
    prunePendingStatusProjectionsLocked();
    for (ProtocolStatusProjection& pending : pending_status_projections_)
    {
        if (pending.value.message_id == projection.value.message_id &&
            sameProtocol(pending.protocol, projection.protocol))
        {
            pending = projection;
            ++pending_status_revision_;
            refreshPersistenceDemandLocked();
            return true;
        }
    }
    if (pending_status_projections_.size() >=
        kPendingStatusProjectionCapacity)
    {
        return false;
    }
    pending_status_projections_.push_back(projection);
    ++pending_status_revision_;
    refreshPersistenceDemandLocked();
    return true;
}

void SdStore::prunePendingStatusProjectionsLocked()
{
    if (pending_status_head_ == 0U)
    {
        return;
    }
    if (pending_status_head_ >= pending_status_projections_.size())
    {
        pending_status_projections_.clear();
        pending_status_head_ = 0U;
        return;
    }
    pending_status_projections_.erase(
        pending_status_projections_.begin(),
        pending_status_projections_.begin() +
            static_cast<std::ptrdiff_t>(pending_status_head_));
    pending_status_head_ = 0U;
}

void SdStore::refreshPersistenceDemandLocked()
{
    persistence_pending_.store(
        pending_status_head_ < pending_status_projections_.size(),
        std::memory_order_release);
}

bool SdStore::flushPendingStatusProjections(std::size_t budget)
{
    flush_status_batch_.clear();
    std::size_t start = 0U;
    uint32_t revision = 0U;
    {
        storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
        if (!state_lock.locked() || !ready_)
        {
            return false;
        }
        prunePendingStatusProjectionsLocked();
        start = pending_status_head_;
        revision = pending_status_revision_;
        const std::size_t end = std::min(
            pending_status_projections_.size(),
            start + std::max<std::size_t>(1U, budget));
        flush_status_batch_.assign(
            pending_status_projections_.begin() +
                static_cast<std::ptrdiff_t>(start),
            pending_status_projections_.begin() +
                static_cast<std::ptrdiff_t>(end));
        if (flush_status_batch_.empty())
        {
            refreshPersistenceDemandLocked();
            return true;
        }
    }

    std::size_t written = 0U;
    for (const ProtocolStatusProjection& projection : flush_status_batch_)
    {
        if (!appendStatusProjection(projection.protocol, projection.value))
        {
            break;
        }
        ++written;
    }

    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked() || !ready_)
    {
        return false;
    }
    if (pending_status_revision_ == revision)
    {
        pending_status_head_ = start + written;
        prunePendingStatusProjectionsLocked();
    }
    refreshPersistenceDemandLocked();
    return written == flush_status_batch_.size();
}

bool SdStore::appendSeenProjection(
    const storage_v2::ReticulumSeenProjection& projection) const
{
    const std::size_t slot_size = storage_v2::reticulumSeenSlotSize();
    if (slot_size > scratch_.size() ||
        !storage_v2::encodeReticulumSeenSlot(projection,
                                             scratch_.data(),
                                             slot_size))
    {
        return false;
    }
    char path[128]{};
    buildProjectionPath(MeshProtocol::Reticulum,
                        "seen.delta",
                        path,
                        sizeof(path));
    auto inspection = journal_.inspect(
        path,
        MeshProtocol::Reticulum,
        storage_v2::JournalKind::ReticulumSeen,
        slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::PartialTail &&
        !repairPartialJournal(path,
                              MeshProtocol::Reticulum,
                              storage_v2::JournalKind::ReticulumSeen,
                              slot_size))
    {
        return false;
    }
    if (!storage_v2::encodeReticulumSeenSlot(projection,
                                             scratch_.data(),
                                             slot_size))
    {
        return false;
    }
    const bool ok = journal_.append(
        path,
        MeshProtocol::Reticulum,
        storage_v2::JournalKind::ReticulumSeen,
        slot_size,
        scratch_.data());
    if (ok)
    {
        maintenance_compaction_requested_.store(
            true,
            std::memory_order_release);
    }
    return ok;
}

bool SdStore::rememberReticulumHash(const uint8_t* hash)
{
    if (hasReticulumLxmfMessageHash(hash))
    {
        return true;
    }
    storage_v2::ReticulumSeenProjection projection{};
    std::memcpy(projection.hash, hash, sizeof(projection.hash));
    if (!appendSeenProjection(projection))
    {
        return false;
    }
    storage_runtime::ScopedRecursiveStateLock state_lock(mutex_);
    if (!state_lock.locked())
    {
        return false;
    }
    if (seen_hot_.size() == kSeenHotCapacity)
    {
        seen_hot_.erase(seen_hot_.begin());
    }
    seen_hot_.push_back(projection);
    return true;
}

storage_v2::ChatCatalogProjection* SdStore::findCatalog(
    const ConversationId& conversation)
{
    for (storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

const storage_v2::ChatCatalogProjection* SdStore::findCatalog(
    const ConversationId& conversation) const
{
    for (const storage_v2::ChatCatalogProjection& projection : catalog_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

storage_v2::ChatReadProjection* SdStore::findReadState(
    const ConversationId& conversation)
{
    for (storage_v2::ChatReadProjection& projection : read_state_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

const storage_v2::ChatReadProjection* SdStore::findReadState(
    const ConversationId& conversation) const
{
    for (const storage_v2::ChatReadProjection& projection : read_state_)
    {
        if (sameConversationKey(projection.conversation, conversation))
        {
            return &projection;
        }
    }
    return nullptr;
}

storage_v2::ChatStatusProjection* SdStore::findStatus(MessageId message_id,
                                                      MeshProtocol protocol)
{
    for (ProtocolStatusProjection& state : statuses_)
    {
        if (sameProtocol(state.protocol, protocol) &&
            state.value.message_id == message_id)
        {
            return &state.value;
        }
    }
    return nullptr;
}

const storage_v2::ChatStatusProjection* SdStore::findStatus(
    MessageId message_id,
    MeshProtocol protocol) const
{
    for (const ProtocolStatusProjection& state : statuses_)
    {
        if (sameProtocol(state.protocol, protocol) &&
            state.value.message_id == message_id)
        {
            return &state.value;
        }
    }
    return nullptr;
}

void SdStore::applyStoredStatus(ChatMessage& message) const
{
    if (message.from != 0U)
    {
        return;
    }
    if (const storage_v2::ChatStatusProjection* status =
            findStatus(message.msg_id, message.protocol))
    {
        message.status = status->status;
    }
}

uint32_t SdStore::countUnreadAfter(const ConversationId& conversation,
                                   uint32_t last_read_sequence) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    uint32_t unread = 0;
    for (uint32_t ordinal = last_read_sequence; ordinal < count; ++ordinal)
    {
        ChatMessage message{};
        uint32_t sequence = 0;
        if (readMessageByOrdinal(conversation,
                                 ordinal,
                                 message,
                                 &sequence) &&
            sequence > last_read_sequence &&
            message.status == MessageStatus::Incoming)
        {
            ++unread;
        }
    }
    return unread;
}

uint32_t SdStore::sequenceForUnread(const ConversationId& conversation,
                                    uint32_t unread) const
{
    const uint32_t count = messageCountOnDisk(conversation);
    if (unread == 0U)
    {
        return count;
    }
    uint32_t incoming = 0;
    for (uint32_t ordinal = count; ordinal > 0U; --ordinal)
    {
        ChatMessage message{};
        uint32_t sequence = 0;
        if (!readMessageByOrdinal(conversation,
                                  ordinal - 1U,
                                  message,
                                  &sequence) ||
            message.status != MessageStatus::Incoming)
        {
            continue;
        }
        ++incoming;
        if (incoming == unread)
        {
            return sequence > 0U ? sequence - 1U : 0U;
        }
    }
    return 0U;
}

MeshProtocol SdStore::normalizeProtocol(MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum : protocol;
}

const char* SdStore::protocolRoot(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    if (protocol == MeshProtocol::MeshCore)
    {
        return kMeshCoreRoot;
    }
    if (protocol == MeshProtocol::Reticulum)
    {
        return kReticulumRoot;
    }
    return kMeshtasticRoot;
}

const char* SdStore::protocolSlug(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    if (protocol == MeshProtocol::MeshCore)
    {
        return "mc";
    }
    if (protocol == MeshProtocol::Reticulum)
    {
        return "rt";
    }
    return "mt";
}

bool SdStore::sameProtocol(MeshProtocol lhs, MeshProtocol rhs)
{
    return normalizeProtocol(lhs) == normalizeProtocol(rhs);
}

bool SdStore::sameStoredMessage(const ChatMessage& lhs,
                                const ChatMessage& rhs)
{
    if (!sameProtocol(lhs.protocol, rhs.protocol) ||
        !sameConversationKey(conversationIdForMessage(lhs),
                             conversationIdForMessage(rhs)))
    {
        return false;
    }
    if (chat::hasReticulumLxmfMessageHash(lhs) &&
        chat::hasReticulumLxmfMessageHash(rhs))
    {
        return std::memcmp(lhs.reticulum_lxmf_hash,
                           rhs.reticulum_lxmf_hash,
                           kReticulumLxmfHashSize) == 0;
    }
    if (lhs.msg_id != 0U || rhs.msg_id != 0U)
    {
        return lhs.msg_id == rhs.msg_id && lhs.from == rhs.from;
    }
    return lhs.from == rhs.from && lhs.timestamp == rhs.timestamp &&
           lhs.text == rhs.text;
}

ConversationMeta SdStore::makeMeta(
    const storage_v2::ChatCatalogProjection& projection)
{
    ConversationMeta meta{};
    meta.id = projection.conversation;
    meta.preview = projection.preview;
    meta.last_timestamp = projection.last_timestamp;
    meta.unread = static_cast<int>(projection.unread);
    meta.reticulum_identity = projection.conversation.reticulum_identity;
    if (projection.conversation.peer == 0U &&
        projection.conversation.protocol != MeshProtocol::Reticulum)
    {
        meta.name = "Broadcast";
    }
    else if (projection.conversation.protocol == MeshProtocol::Reticulum &&
             hasReticulumDestinationIdentity(
                 projection.conversation.reticulum_identity))
    {
        char name[12]{};
        std::snprintf(name,
                      sizeof(name),
                      "%02X%02X%02X%02X",
                      projection.conversation.reticulum_identity
                          .destination_hash[0],
                      projection.conversation.reticulum_identity
                          .destination_hash[1],
                      projection.conversation.reticulum_identity
                          .destination_hash[2],
                      projection.conversation.reticulum_identity
                          .destination_hash[3]);
        meta.name = name;
    }
    else
    {
        char name[16]{};
        std::snprintf(name,
                      sizeof(name),
                      "%04lX",
                      static_cast<unsigned long>(projection.conversation.peer &
                                                 0xFFFFU));
        meta.name = name;
    }
    return meta;
}

void SdStore::buildConversationDirectory(const ConversationId& input,
                                         char* out,
                                         std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    ConversationId conversation = input;
    conversation.protocol = normalizeProtocol(conversation.protocol);
    if (conversation.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(conversation.reticulum_identity))
    {
        char hash[2U * kReticulumPeerHashSize + 1U]{};
        hashToHex(conversation.reticulum_identity.destination_hash,
                  hash,
                  sizeof(hash));
        std::snprintf(out,
                      out_len,
                      "%s/conversations/d_%s",
                      protocolRoot(conversation.protocol),
                      hash);
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%s/conversations/c%02X_p%08lX",
                  protocolRoot(conversation.protocol),
                  static_cast<unsigned>(conversation.channel),
                  static_cast<unsigned long>(conversation.peer));
}

void SdStore::buildMessageSegmentPath(const ConversationId& conversation,
                                      uint32_t segment,
                                      char* out,
                                      std::size_t out_len)
{
    char directory[112]{};
    buildConversationDirectory(conversation, directory, sizeof(directory));
    std::snprintf(out,
                  out_len,
                  "%s/%04lu.msg",
                  directory,
                  static_cast<unsigned long>(segment));
}

void SdStore::buildProjectionPath(MeshProtocol protocol,
                                  const char* name,
                                  char* out,
                                  std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    std::snprintf(out, out_len, "%s/%s", protocolRoot(protocol), name);
}

bool SdStore::ensureDirectory(const char* path)
{
    return path && path[0] != '\0' &&
           (storage_runtime::sd_is_directory(path) ||
            storage_runtime::sd_mkdir(path));
}

bool SdStore::removeTree(const char* path)
{
    if (!path || std::strncmp(path, kRoot, std::strlen(kRoot)) != 0)
    {
        return false;
    }
    if (!storage_runtime::sd_exists(path))
    {
        return true;
    }
    if (!storage_runtime::sd_is_directory(path))
    {
        return storage_runtime::sd_remove(path);
    }
    storage_runtime::SdRuntimeDir directory;
    if (!directory.open(path))
    {
        return false;
    }
    char name[96]{};
    bool is_directory = false;
    bool ok = true;
    while (directory.read_next(name, sizeof(name), &is_directory))
    {
        char child[160]{};
        std::snprintf(child, sizeof(child), "%s/%s", path, name);
        ok = (is_directory ? removeTree(child)
                           : storage_runtime::sd_remove(child)) &&
             ok;
    }
    directory.close();
    return storage_runtime::sd_rmdir(path) && ok;
}

} // namespace chat
