#include "platform/esp/common/storage/storage_maintenance_state_machine.h"

#include <cassert>

using namespace platform::esp::common::storage;

int main()
{
    StorageMaintenanceStateMachine machine;

    StorageMaintenanceCommand command =
        machine.arm(100U, StorageStartupGate::DisplayTransaction, false);
    assert(command.kind == StorageMaintenanceCommandKind::None);
    assert(machine.snapshot().state ==
           StorageRuntimeState::WaitingStartupGate);
    assert(machine.snapshot().pending_operation == StorageOperation::Hydrate);
    assert(isInitialHydrationPending(machine.snapshot()));

    command = machine.tick(101U, false, false, false);
    assert(command.kind == StorageMaintenanceCommandKind::None);

    command = machine.tick(102U, false, false, true);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Hydrate);
    assert(command.generation == 1U);
    assert(machine.snapshot().active_operation == StorageOperation::Hydrate);
    assert(isInitialHydrationPending(machine.snapshot()));

    machine.complete(StorageOperationResult::failure(
                         StorageOperationResultKind::StaleGeneration,
                         StorageOperation::Hydrate,
                         command.generation + 1U),
                     103U);
    assert(machine.snapshot().state == StorageRuntimeState::Hydrating);

    machine.complete(StorageOperationResult::inProgressResult(
                         StorageOperation::Hydrate,
                         command.generation),
                     104U);
    command = machine.tick(105U, false, false, true);
    assert(command.kind == StorageMaintenanceCommandKind::Step);
    assert(command.generation == 1U);

    machine.complete(StorageOperationResult::failure(
                         StorageOperationResultKind::IoError,
                         StorageOperation::Hydrate,
                         command.generation),
                     200U);
    assert(machine.snapshot().state == StorageRuntimeState::Backoff);
    assert(machine.snapshot().pending_operation == StorageOperation::Hydrate);
    assert(machine.snapshot().retry_attempt == 1U);
    assert(machine.snapshot().retry_due_ms == 2200U);
    assert(isInitialHydrationPending(machine.snapshot()));

    command = machine.tick(2199U, false, false, true);
    assert(command.kind == StorageMaintenanceCommandKind::None);
    command = machine.tick(2200U, false, false, true);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.generation == 1U);

    machine.complete(StorageOperationResult::completedResult(
                         StorageOperation::Hydrate,
                         command.generation),
                     2300U);
    assert(machine.snapshot().state == StorageRuntimeState::Ready);
    assert(machine.snapshot().active_operation == StorageOperation::None);
    assert(machine.snapshot().pending_operation == StorageOperation::None);
    assert(!isInitialHydrationPending(machine.snapshot()));
    assert(machine.takeHydrationReadyGeneration() == 1U);
    assert(machine.takeHydrationReadyGeneration() == 0U);

    StorageMaintenanceDemand demand{};
    demand.compaction_pending = true;
    command = machine.tick(2301U, true, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::None);
    command = machine.tick(3801U, true, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Compact);
    assert(command.generation == 2U);

    machine.complete(StorageOperationResult::completedResult(
                         StorageOperation::Compact,
                         command.generation),
                     3900U);
    assert(machine.snapshot().state == StorageRuntimeState::Ready);
    assert(machine.tick(10000U, true, false, true).kind ==
           StorageMaintenanceCommandKind::None);

    demand = {};
    demand.persistence_pending = true;
    command = machine.tick(10001U, false, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Persist);
    machine.complete(StorageOperationResult::completedResult(
                         StorageOperation::Persist,
                         command.generation),
                     10002U);
    assert(machine.snapshot().state == StorageRuntimeState::Ready);

    demand.compaction_pending = true;
    command = machine.tick(10003U, false, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Persist);
    machine.complete(StorageOperationResult::completedResult(
                         StorageOperation::Persist,
                         command.generation),
                     10004U);
    command = machine.tick(10005U, true, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::None);
    command = machine.tick(11505U, true, false, true, demand);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Compact);

    const StorageRetryPolicy retry_policy{};
    assert(retry_policy.delayForAttempt(1U) == 2000U);
    assert(retry_policy.delayForAttempt(2U) == 4000U);
    assert(retry_policy.delayForAttempt(6U) == 60000U);

    StorageMaintenanceStateMachine immediate_machine;
    command = immediate_machine.arm(
        0U,
        StorageStartupGate::Immediate,
        true);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.generation == 1U);
    assert(isInitialHydrationPending(immediate_machine.snapshot()));

    StorageRetryPolicy bounded_retry{};
    bounded_retry.maximum_attempts = 2U;
    StorageMaintenanceStateMachine bounded_machine(bounded_retry);
    command = bounded_machine.arm(
        0U,
        StorageStartupGate::Immediate,
        true);
    bounded_machine.complete(
        StorageOperationResult::failure(StorageOperationResultKind::IoError,
                                        StorageOperation::Hydrate,
                                        command.generation),
        10U);
    command = bounded_machine.tick(2010U, false, false, true);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    bounded_machine.complete(
        StorageOperationResult::failure(StorageOperationResultKind::IoError,
                                        StorageOperation::Hydrate,
                                        command.generation),
        2020U);
    assert(bounded_machine.snapshot().state == StorageRuntimeState::Done);
    assert(!isInitialHydrationPending(bounded_machine.snapshot()));

    StorageMaintenanceStateMachine cancelled_machine;
    command = cancelled_machine.arm(
        0U,
        StorageStartupGate::Immediate,
        true);
    cancelled_machine.complete(
        StorageOperationResult::failure(StorageOperationResultKind::Cancelled,
                                        StorageOperation::Hydrate,
                                        command.generation),
        1U);
    assert(cancelled_machine.snapshot().state == StorageRuntimeState::Done);
    assert(cancelled_machine.snapshot().pending_operation ==
           StorageOperation::None);
    assert(!isInitialHydrationPending(cancelled_machine.snapshot()));
    command = cancelled_machine.arm(
        2U,
        StorageStartupGate::Immediate,
        true);
    assert(command.kind == StorageMaintenanceCommandKind::Begin);
    assert(command.operation == StorageOperation::Hydrate);
    assert(command.generation == 1U);
    return 0;
}
