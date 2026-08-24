#include "app/config_persistence_runtime.h"

#include <cassert>

namespace
{

void testDebounceAndCompletion()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    const app::ConfigPersistenceSubmission submission =
        runtime.submit(app::AppConfigChangeSet::map(), 100U);
    assert(submission.queued);
    assert(submission.generation == 1U);
    assert(submission.changes.containsAll(app::AppConfigChangeSet::map()));
    assert(runtime.state() == app::ConfigPersistenceState::Debouncing);

    app::ConfigPersistenceWork work;
    assert(!runtime.takeDue(349U, work));
    assert(runtime.takeDue(350U, work));
    assert(work.generation == 1U);
    assert(work.changes.containsAll(app::AppConfigChangeSet::map()));
    assert(runtime.busy());

    assert(runtime.complete(work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            351U) ==
           app::ConfigPersistenceResultKind::Completed);
    assert(!runtime.busy());
    assert(!runtime.hasPending());
    assert(runtime.lastCompletedGeneration() == 1U);
    assert(runtime.state() == app::ConfigPersistenceState::Idle);
}

void testLaterEditRemainsQueuedAfterSuccessfulSave()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime.submit(app::AppConfigChangeSet::map(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));

    const app::ConfigPersistenceSubmission next =
        runtime.submit(app::AppConfigChangeSet::gps(), 300U);
    assert(next.queued);
    assert(next.generation == 2U);
    assert(runtime.hasPending());

    assert(runtime.complete(first_work.generation,
                            app::ConfigPersistenceResultKind::Completed,
                            400U) ==
           app::ConfigPersistenceResultKind::Completed);
    assert(runtime.hasPending());
    assert(runtime.state() == app::ConfigPersistenceState::Debouncing);
    assert(runtime.lastCompletedGeneration() == 1U);

    app::ConfigPersistenceWork next_work;
    assert(!runtime.takeDue(649U, next_work));
    assert(runtime.takeDue(650U, next_work));
    assert(next_work.generation == 2U);
    assert(next_work.changes.contains(app::AppConfigChangeDomain::Gps));
    assert(!next_work.changes.contains(app::AppConfigChangeDomain::Map));
}

void testFailureRetriesThePendingChangeSet()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime.submit(app::AppConfigChangeSet::map(), 10U).queued);
    app::ConfigPersistenceWork work;
    assert(runtime.takeDue(260U, work));

    assert(runtime.complete(work.generation,
                            app::ConfigPersistenceResultKind::IoError,
                            300U) ==
           app::ConfigPersistenceResultKind::IoError);
    assert(runtime.hasPending());
    assert(runtime.pendingGeneration() == work.generation);
    assert(runtime.state() == app::ConfigPersistenceState::Backoff);
    assert(!runtime.takeDue(1299U, work));
    assert(runtime.takeDue(1300U, work));
    assert(work.generation == 1U);
    assert(work.changes.contains(app::AppConfigChangeDomain::Map));
    assert(!work.changes.contains(app::AppConfigChangeDomain::Gps));
}

void testFailureMergesNewerPendingChanges()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime.submit(app::AppConfigChangeSet::map(), 0U).queued);
    app::ConfigPersistenceWork work;
    assert(runtime.takeDue(250U, work));
    assert(runtime.submit(app::AppConfigChangeSet::gps(), 300U).queued);

    assert(runtime.complete(work.generation,
                            app::ConfigPersistenceResultKind::IoError,
                            400U) ==
           app::ConfigPersistenceResultKind::IoError);
    assert(runtime.takeDue(1400U, work));
    assert(work.generation == 2U);
    assert(work.changes.contains(app::AppConfigChangeDomain::Map));
    assert(work.changes.contains(app::AppConfigChangeDomain::Gps));
}

void testStaleCompletionCannotChangeState()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime.submit(app::AppConfigChangeSet::map(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));
    assert(runtime.submit(app::AppConfigChangeSet::gps(), 300U).queued);
    assert(runtime.complete(2U,
                            app::ConfigPersistenceResultKind::Completed,
                            301U) ==
           app::ConfigPersistenceResultKind::StaleGeneration);
    assert(runtime.busy());
    assert(runtime.activeGeneration() == first_work.generation);
}

void testCriticalSaveSkipsDebounce()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime
               .submit(app::AppConfigChangeSet::map(),
                       1000U,
                       app::ConfigPersistenceUrgency::Immediate)
               .queued);
    app::ConfigPersistenceWork work;
    assert(runtime.takeDue(1000U, work));
    assert(work.generation == 1U);
}

void testCriticalUrgencySurvivesGenerationHandoff()
{
    app::ConfigPersistenceRuntime runtime;
    runtime.initialize();

    assert(runtime.submit(app::AppConfigChangeSet::map(), 0U).queued);
    app::ConfigPersistenceWork first_work;
    assert(runtime.takeDue(250U, first_work));

    assert(runtime
               .submit(app::AppConfigChangeSet::gps(),
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
    assert(critical_work.changes.contains(app::AppConfigChangeDomain::Gps));
}

} // namespace

int main()
{
    testDebounceAndCompletion();
    testLaterEditRemainsQueuedAfterSuccessfulSave();
    testFailureRetriesThePendingChangeSet();
    testFailureMergesNewerPendingChanges();
    testStaleCompletionCannotChangeState();
    testCriticalSaveSkipsDebounce();
    testCriticalUrgencySurvivesGenerationHandoff();
    return 0;
}
