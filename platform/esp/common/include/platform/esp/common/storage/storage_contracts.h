#pragma once

#include "sys/persistence_contracts.h"

#include <cstdint>

namespace platform::esp::common::storage
{

enum class StorageOperation : uint8_t
{
    None,
    Hydrate,
    Persist,
    Compact,
};

using StorageOperationGeneration = sys::PersistenceGeneration;
using StorageOperationResultKind = sys::PersistenceResultKind;

struct StorageOperationResult
{
    StorageOperationResultKind kind = StorageOperationResultKind::IoError;
    StorageOperation operation = StorageOperation::None;
    StorageOperationGeneration generation = 0U;

    bool completed() const
    {
        return kind == StorageOperationResultKind::Completed;
    }

    bool inProgress() const
    {
        return kind == StorageOperationResultKind::InProgress;
    }

    bool retryable() const
    {
        return kind == StorageOperationResultKind::StateBusy ||
               kind == StorageOperationResultKind::DeviceUnavailable ||
               kind == StorageOperationResultKind::RetryLater ||
               kind == StorageOperationResultKind::IoError;
    }

    static StorageOperationResult completedResult(
        StorageOperation operation,
        StorageOperationGeneration generation)
    {
        return {StorageOperationResultKind::Completed, operation, generation};
    }

    static StorageOperationResult inProgressResult(
        StorageOperation operation,
        StorageOperationGeneration generation)
    {
        return {StorageOperationResultKind::InProgress, operation, generation};
    }

    static StorageOperationResult failure(
        StorageOperationResultKind kind,
        StorageOperation operation,
        StorageOperationGeneration generation)
    {
        return {kind, operation, generation};
    }
};

struct StorageOperationBudget
{
    // A step is deliberately expressed in logical work items rather than
    // milliseconds. Physical adapters can then keep each transaction bounded
    // without making the owner depend on a particular clock or filesystem.
    uint8_t max_work_items = 1U;
};

struct StorageMaintenanceDemand
{
    bool persistence_pending = false;
    bool compaction_pending = false;
};

struct StorageRetryPolicy
{
    uint32_t base_delay_ms = 2000U;
    uint32_t maximum_delay_ms = 60000U;
    uint8_t maximum_attempts = 0U;

    uint32_t delayForAttempt(uint8_t attempt) const
    {
        if (attempt == 0U)
        {
            return 0U;
        }

        uint32_t delay = base_delay_ms;
        for (uint8_t shift = 1U; shift < attempt && delay < maximum_delay_ms;
             ++shift)
        {
            if (delay > maximum_delay_ms / 2U)
            {
                delay = maximum_delay_ms;
                break;
            }
            delay *= 2U;
        }
        return delay > maximum_delay_ms ? maximum_delay_ms : delay;
    }
};

enum class StorageRuntimeState : uint8_t
{
    Dormant,
    WaitingStartupGate,
    Hydrating,
    Ready,
    WaitingIdle,
    Persisting,
    Compacting,
    Backoff,
    Done,
};

struct StorageRuntimeSnapshot
{
    StorageRuntimeState state = StorageRuntimeState::Dormant;
    StorageOperation active_operation = StorageOperation::None;
    StorageOperation pending_operation = StorageOperation::None;
    StorageOperationGeneration generation = 0U;
    uint8_t retry_attempt = 0U;
    uint32_t retry_due_ms = 0U;
    bool startup_gate_satisfied = false;
};

// This is the only contract the maintenance owner sees for a storage backend.
// Physical sessions, SPI arbitration, filesystem handles, and task handles
// remain implementation details of the adapter.
class ISemanticStorageAdapter
{
  public:
    virtual ~ISemanticStorageAdapter() = default;

    // Begin starts a new operation for a new generation, or resumes the
    // backend cursor for the same operation and generation after a retryable
    // result.
    // A retry must not discard partially completed work merely because the
    // previous step released a physical/device lease.
    virtual StorageOperationResult begin(
        StorageOperation operation,
        StorageOperationGeneration generation) = 0;

    // A retryable result may release a transient device/transaction lease,
    // but the backend's logical maintenance ownership remains held until the
    // operation completes, is cancelled, or reaches an explicit failed
    // boundary. The adapter retains enough operation state for begin() to
    // resume the same generation without competing with foreground writes.
    virtual StorageOperationResult step(
        StorageOperation operation,
        StorageOperationGeneration generation,
        const StorageOperationBudget& budget) = 0;

    virtual void cancelAtStepBoundary(
        StorageOperation operation,
        StorageOperationGeneration generation) = 0;
};

} // namespace platform::esp::common::storage
