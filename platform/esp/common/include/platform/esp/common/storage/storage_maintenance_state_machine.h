#pragma once

#include "platform/esp/common/storage/storage_contracts.h"

#include <algorithm>
#include <cstdint>

namespace platform::esp::common::storage
{

enum class StorageStartupGate : uint8_t
{
    Immediate,
    DisplayTransaction,
};

enum class StorageMaintenanceCommandKind : uint8_t
{
    None,
    Begin,
    Step,
};

struct StorageMaintenanceCommand
{
    StorageMaintenanceCommandKind kind = StorageMaintenanceCommandKind::None;
    StorageOperation operation = StorageOperation::None;
    StorageOperationGeneration generation = 0U;
};

// Optional interactive reads must yield until the initial authoritative
// projection has either been installed or reached a terminal result. This is
// intentionally narrower than the runtime's general maintenance activity:
// persistence and compaction do not block interactive reads.
constexpr bool isInitialHydrationPending(const StorageRuntimeSnapshot& snapshot)
{
    return snapshot.pending_operation == StorageOperation::Hydrate &&
           (snapshot.state == StorageRuntimeState::WaitingStartupGate ||
            snapshot.state == StorageRuntimeState::Hydrating ||
            snapshot.state == StorageRuntimeState::Backoff);
}

class StorageMaintenanceStateMachine final
{
  public:
    explicit StorageMaintenanceStateMachine(
        StorageRetryPolicy retry_policy = {})
        : retry_policy_(retry_policy)
    {
    }

    StorageMaintenanceCommand arm(uint32_t now_ms,
                                  StorageStartupGate startup_gate,
                                  bool startup_gate_satisfied)
    {
        (void)now_ms;
        if (snapshot_.state == StorageRuntimeState::Done)
        {
            snapshot_ = {};
            retry_operation_ = StorageOperation::Hydrate;
            idle_since_ms_ = 0U;
            hydration_ready_generation_ = 0U;
        }
        if (snapshot_.state != StorageRuntimeState::Dormant)
        {
            return {};
        }

        snapshot_.generation = 0U;
        snapshot_.pending_operation = StorageOperation::Hydrate;
        snapshot_.startup_gate_satisfied = startup_gate_satisfied;
        if (startup_gate == StorageStartupGate::DisplayTransaction &&
            !startup_gate_satisfied)
        {
            snapshot_.state = StorageRuntimeState::WaitingStartupGate;
            return {};
        }

        return beginOperation(StorageOperation::Hydrate, true);
    }

    StorageMaintenanceCommand tick(
        uint32_t now_ms,
        bool is_sleeping,
        bool saver_active,
        bool startup_gate_satisfied,
        StorageMaintenanceDemand demand = {})
    {
        snapshot_.startup_gate_satisfied |= startup_gate_satisfied;

        switch (snapshot_.state)
        {
        case StorageRuntimeState::WaitingStartupGate:
            if (snapshot_.startup_gate_satisfied)
            {
                return beginOperation(StorageOperation::Hydrate, true);
            }
            return {};

        case StorageRuntimeState::Ready:
            snapshot_.state = StorageRuntimeState::WaitingIdle;
            [[fallthrough]];

        case StorageRuntimeState::WaitingIdle:
            if (demand.compaction_pending && is_sleeping && !saver_active)
            {
                if (idle_since_ms_ == 0U)
                {
                    idle_since_ms_ = now_ms;
                    return {};
                }
                if (static_cast<uint32_t>(now_ms - idle_since_ms_) <
                    kIdleStableMs)
                {
                    return {};
                }
                return beginOperation(StorageOperation::Compact, true);
            }
            if (demand.persistence_pending)
            {
                idle_since_ms_ = 0U;
                return beginOperation(StorageOperation::Persist, true);
            }
            if (demand.compaction_pending)
            {
                idle_since_ms_ = 0U;
                return {};
            }
            if (!is_sleeping || saver_active)
            {
                idle_since_ms_ = 0U;
                return {};
            }
            if (idle_since_ms_ == 0U)
            {
                idle_since_ms_ = now_ms;
                return {};
            }
            if (static_cast<uint32_t>(now_ms - idle_since_ms_) <
                kIdleStableMs)
            {
                return {};
            }
            return {};

        case StorageRuntimeState::Backoff:
            if (!deadlineReached(now_ms, snapshot_.retry_due_ms))
            {
                return {};
            }
            return beginOperation(retry_operation_, false);

        case StorageRuntimeState::Hydrating:
            return stepOperation(StorageOperation::Hydrate);

        case StorageRuntimeState::Persisting:
            return stepOperation(StorageOperation::Persist);

        case StorageRuntimeState::Compacting:
            return stepOperation(StorageOperation::Compact);

        case StorageRuntimeState::Dormant:
        case StorageRuntimeState::Done:
        default:
            return {};
        }
    }

    void complete(const StorageOperationResult& result, uint32_t now_ms)
    {
        if (result.generation != snapshot_.generation ||
            result.operation != snapshot_.active_operation)
        {
            return;
        }

        if (result.kind == StorageOperationResultKind::InProgress)
        {
            return;
        }

        if (result.completed())
        {
            snapshot_.retry_attempt = 0U;
            snapshot_.retry_due_ms = 0U;
            idle_since_ms_ = 0U;
            if (result.operation == StorageOperation::Hydrate)
            {
                snapshot_.state = StorageRuntimeState::Ready;
                snapshot_.active_operation = StorageOperation::None;
                snapshot_.pending_operation = StorageOperation::None;
                hydration_ready_generation_ = snapshot_.generation;
            }
            else if (result.operation == StorageOperation::Persist)
            {
                snapshot_.state = StorageRuntimeState::Ready;
                snapshot_.active_operation = StorageOperation::None;
                snapshot_.pending_operation = StorageOperation::None;
            }
            else if (result.operation == StorageOperation::Compact)
            {
                snapshot_.state = StorageRuntimeState::Ready;
                snapshot_.active_operation = StorageOperation::None;
                snapshot_.pending_operation = StorageOperation::None;
            }
            return;
        }

        if (result.kind == StorageOperationResultKind::StaleGeneration)
        {
            return;
        }

        if (result.kind == StorageOperationResultKind::Cancelled)
        {
            snapshot_.state = StorageRuntimeState::Done;
            snapshot_.active_operation = StorageOperation::None;
            snapshot_.pending_operation = StorageOperation::None;
            snapshot_.retry_due_ms = 0U;
            return;
        }

        retry_operation_ = result.operation;
        snapshot_.state = StorageRuntimeState::Backoff;
        snapshot_.active_operation = StorageOperation::None;
        snapshot_.pending_operation = result.operation;
        snapshot_.retry_attempt = static_cast<uint8_t>(
            std::min<unsigned>(snapshot_.retry_attempt + 1U, 255U));
        if (retry_policy_.maximum_attempts != 0U &&
            snapshot_.retry_attempt >= retry_policy_.maximum_attempts)
        {
            snapshot_.state = StorageRuntimeState::Done;
            snapshot_.active_operation = StorageOperation::None;
            snapshot_.pending_operation = StorageOperation::None;
            snapshot_.retry_due_ms = 0U;
            return;
        }
        snapshot_.retry_due_ms =
            now_ms + retry_policy_.delayForAttempt(snapshot_.retry_attempt);
    }

    StorageRuntimeSnapshot snapshot() const
    {
        return snapshot_;
    }

    void stop()
    {
        snapshot_.state = StorageRuntimeState::Done;
        snapshot_.active_operation = StorageOperation::None;
        snapshot_.pending_operation = StorageOperation::None;
        snapshot_.retry_due_ms = 0U;
        retry_operation_ = StorageOperation::None;
        idle_since_ms_ = 0U;
        hydration_ready_generation_ = 0U;
    }

    StorageOperationGeneration takeHydrationReadyGeneration()
    {
        const StorageOperationGeneration generation =
            hydration_ready_generation_;
        hydration_ready_generation_ = 0U;
        return generation;
    }

  private:
    static constexpr uint32_t kIdleStableMs = 1500U;

    static bool deadlineReached(uint32_t now_ms, uint32_t deadline_ms)
    {
        return static_cast<int32_t>(now_ms - deadline_ms) >= 0;
    }

    StorageMaintenanceCommand beginOperation(StorageOperation operation,
                                             bool new_generation)
    {
        if (new_generation)
        {
            ++snapshot_.generation;
            if (snapshot_.generation == 0U)
            {
                snapshot_.generation = 1U;
            }
        }
        snapshot_.state = operation == StorageOperation::Hydrate
                              ? StorageRuntimeState::Hydrating
                          : operation == StorageOperation::Persist
                              ? StorageRuntimeState::Persisting
                              : StorageRuntimeState::Compacting;
        snapshot_.active_operation = operation;
        snapshot_.pending_operation = operation;
        snapshot_.retry_due_ms = 0U;
        return {StorageMaintenanceCommandKind::Begin,
                operation,
                snapshot_.generation};
    }

    StorageMaintenanceCommand stepOperation(StorageOperation operation) const
    {
        return {StorageMaintenanceCommandKind::Step,
                operation,
                snapshot_.generation};
    }

    StorageRetryPolicy retry_policy_{};
    StorageRuntimeSnapshot snapshot_{};
    StorageOperation retry_operation_ = StorageOperation::Hydrate;
    StorageOperationGeneration hydration_ready_generation_ = 0U;
    uint32_t idle_since_ms_ = 0U;
};

} // namespace platform::esp::common::storage
