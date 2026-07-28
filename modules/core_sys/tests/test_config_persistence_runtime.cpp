#include "app/config_persistence_runtime.h"

#include <cassert>

namespace
{

app::AppConfig changedMap(app::AppConfig value, uint8_t source)
{
    value.map_source = source;
    return value;
}

void testDebounceAndCompletion()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig desired = changedMap(baseline, 1U);
    const app::ConfigPersistenceSubmission submission =
        runtime.submit(desired, app::AppConfigChangeSet::none(), 100U);
    assert(submission.queued);
    assert(submission.generation == 1U);
    assert(submission.changes.containsAll(app::AppConfigChangeSet::map()));
    assert(!runtime.hasPending() || runtime.state() ==
                                        app::ConfigPersistenceState::Debouncing);

    app::ConfigPersistenceWork work;
    assert(!runtime.takeDue(349U, work));
    assert(runtime.takeDue(350U, work));
    assert(work.snapshot != nullptr);
    assert(work.snapshot->map_source == 1U);
    assert(work.generation == 1U);
    assert(runtime.busy());

    assert(runtime.complete(work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            351U) ==
           app::ConfigPersistenceResultKind::Completed);
    assert(!runtime.busy());
    assert(!runtime.hasPending());
    assert(runtime.baselineValid());
    assert(runtime.lastCompletedGeneration() == 1U);
    assert(runtime.state() == app::ConfigPersistenceState::Idle);
}

void testPendingRollbackIsReconciledAgainstNewBaseline()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig first = changedMap(baseline, 1U);
    assert(runtime.submit(first, app::AppConfigChangeSet::none(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));

    const app::ConfigPersistenceSubmission rollback =
        runtime.submit(baseline, app::AppConfigChangeSet::none(), 300U);
    assert(rollback.queued);
    assert(rollback.generation == 2U);
    assert(runtime.hasPending());

    assert(runtime.complete(first_work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            400U) ==
           app::ConfigPersistenceResultKind::Completed);
    assert(runtime.hasPending());
    assert(runtime.state() == app::ConfigPersistenceState::Debouncing);
    assert(runtime.lastCompletedGeneration() == 1U);

    app::ConfigPersistenceWork rollback_work;
    assert(!runtime.takeDue(649U, rollback_work));
    assert(runtime.takeDue(650U, rollback_work));
    assert(rollback_work.generation == 2U);
    assert(rollback_work.snapshot->map_source == 0U);
}

void testFailureRetriesTheFailedPayload()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig desired = changedMap(baseline, 2U);
    assert(runtime.submit(desired, app::AppConfigChangeSet::none(), 10U).queued);
    app::ConfigPersistenceWork work;
    assert(runtime.takeDue(260U, work));

    assert(runtime.complete(work.generation,
                            app::ConfigPersistenceResultKind::IoError,
                            300U) ==
           app::ConfigPersistenceResultKind::IoError);
    assert(runtime.baselineValid());
    assert(runtime.hasPending());
    assert(runtime.pendingGeneration() == work.generation);
    assert(runtime.state() == app::ConfigPersistenceState::Backoff);
    assert(!runtime.takeDue(1299U, work));
    assert(runtime.takeDue(1300U, work));
    assert(work.generation == 1U);
    assert(work.snapshot->map_source == 2U);
    assert(work.changes.contains(app::AppConfigChangeDomain::Map));
    assert(!work.changes.contains(app::AppConfigChangeDomain::Gps));
}

void testStaleCompletionCannotChangeState()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig first = changedMap(baseline, 1U);
    assert(runtime.submit(first, app::AppConfigChangeSet::none(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));

    const app::AppConfig second = changedMap(baseline, 2U);
    assert(runtime.submit(second, app::AppConfigChangeSet::none(), 300U).queued);
    assert(runtime.complete(2U,
                            app::ConfigPersistenceResultKind::Completed,
                            301U) ==
           app::ConfigPersistenceResultKind::StaleGeneration);
    assert(runtime.busy());
    assert(runtime.activeGeneration() == first_work.generation);
    assert(runtime.complete(first_work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            400U) ==
           app::ConfigPersistenceResultKind::Completed);
    assert(runtime.hasPending());
    assert(runtime.takeDue(649U, first_work) == false);
    assert(runtime.takeDue(650U, first_work));
    assert(first_work.generation == 2U);
    assert(first_work.snapshot->map_source == 2U);
}

void testCriticalSaveSkipsDebounce()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig desired = changedMap(baseline, 3U);
    assert(runtime.submit(desired,
                          app::AppConfigChangeSet::none(),
                          1000U,
                          app::ConfigPersistenceUrgency::Immediate)
               .queued);
    app::ConfigPersistenceWork work;
    assert(runtime.takeDue(1000U, work));
    assert(work.generation == 1U);
}

void testCriticalUrgencySurvivesGenerationHandoff()
{
    app::AppConfig baseline;
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize(baseline);

    const app::AppConfig first = changedMap(baseline, 1U);
    assert(runtime.submit(first, app::AppConfigChangeSet::none(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));

    const app::AppConfig critical = changedMap(baseline, 2U);
    assert(runtime.submit(critical,
                          app::AppConfigChangeSet::none(),
                          300U,
                          app::ConfigPersistenceUrgency::Immediate)
               .queued);
    assert(runtime.complete(first_work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            400U) ==
           app::ConfigPersistenceResultKind::Completed);

    app::ConfigPersistenceWork critical_work;
    assert(runtime.takeDue(400U, critical_work));
    assert(critical_work.generation == 2U);
    assert(critical_work.snapshot->map_source == 2U);
}

} // namespace

int main()
{
    testDebounceAndCompletion();
    testPendingRollbackIsReconciledAgainstNewBaseline();
    testFailureRetriesTheFailedPayload();
    testStaleCompletionCannotChangeState();
    testCriticalSaveSkipsDebounce();
    testCriticalUrgencySurvivesGenerationHandoff();
    return 0;
}
