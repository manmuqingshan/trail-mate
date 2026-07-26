#include "sys/bus_access_scope.h"
#include "sys/runtime_async.h"

#include <cassert>
#include <utility>

namespace
{

class FakeWorker final : public sys::runtime::IActiveWorker
{
  public:
    void tick(uint32_t now_ms) override
    {
        last_tick_ms = now_ms;
    }

    bool submit(const sys::runtime::RuntimeCommand& command) override
    {
        submitted = true;
        last_command = command;
        return accept;
    }

    bool accept = true;
    bool submitted = false;
    uint32_t last_tick_ms = 0;
    sys::runtime::RuntimeCommand last_command{};
};

class FakeUiEffectSink final : public sys::runtime::IUiEffectSink
{
  public:
    bool apply(const sys::runtime::RuntimeUiEffect& effect) override
    {
        applied = true;
        last_effect = effect;
        return true;
    }

    bool applied = false;
    sys::runtime::RuntimeUiEffect last_effect{};
};

class FakeBusArbiter final : public sys::runtime::IBusArbiter
{
  public:
    bool acquire_ok = true;
    int acquire_count = 0;
    int release_count = 0;

    sys::runtime::BusAcquireResult acquire(
        const sys::runtime::BusAcquireRequest& request) override
    {
        ++acquire_count;
        last_request = request;
        sys::runtime::BusAcquireResult result{};
        result.status = acquire_ok ? sys::runtime::BusAcquireStatus::Acquired
                                   : sys::runtime::BusAcquireStatus::TimedOut;
        result.token.resource = request.resource;
        result.token.owner = request.command_id;
        result.token.valid = acquire_ok;
        result.diagnostics = {};
        result.diagnostics.resource = request.resource;
        result.diagnostics.owner = 77;
        result.diagnostics.command_id = request.command_id;
        result.diagnostics.policy = request.policy;
        return result;
    }

    void release(const sys::runtime::BusAccessToken& token) override
    {
        assert(token.valid);
        ++release_count;
    }

    sys::runtime::StorageHealthState health() const override
    {
        return {};
    }

    sys::runtime::BusAcquireRequest last_request{};
};

void test_priority_pop_order()
{
    sys::runtime::FixedCommandQueue<4> queue;

    sys::runtime::RuntimeCommand background{};
    background.command_id = 1;
    background.kind = sys::runtime::RuntimeCommandKind::PersistenceSave;
    background.priority = sys::runtime::RuntimePriority::Background;
    background.created_at_ms = 1;

    sys::runtime::RuntimeCommand interactive{};
    interactive.command_id = 2;
    interactive.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    interactive.priority = sys::runtime::RuntimePriority::Interactive;
    interactive.created_at_ms = 2;

    assert(queue.enqueue(background));
    assert(queue.enqueue(interactive));

    sys::runtime::RuntimeCommand out{};
    assert(queue.popNext(10, out));
    assert(out.command_id == 2);
    assert(queue.popNext(10, out));
    assert(out.command_id == 1);
}

void test_dedupe_replace()
{
    sys::runtime::FixedCommandQueue<4> queue;

    sys::runtime::RuntimeCommand first{};
    first.command_id = 1;
    first.dedupe_key = 42;
    first.generation = 1;

    sys::runtime::RuntimeCommand replacement = first;
    replacement.command_id = 2;
    replacement.generation = 2;

    assert(queue.enqueueOrReplaceDedupe(first));
    assert(queue.enqueueOrReplaceDedupe(replacement));
    assert(queue.size() == 1);

    sys::runtime::RuntimeCommand out{};
    assert(queue.popNext(0, out));
    assert(out.command_id == 2);
    assert(out.generation == 2);
}

void test_cancel_generation()
{
    sys::runtime::FixedCommandQueue<4> queue;

    sys::runtime::RuntimeCommand a{};
    a.command_id = 1;
    a.generation = 7;
    sys::runtime::RuntimeCommand b{};
    b.command_id = 2;
    b.generation = 8;

    assert(queue.enqueue(a));
    assert(queue.enqueue(b));
    assert(queue.cancelGeneration(7) == 1);
    assert(queue.size() == 1);

    sys::runtime::RuntimeCommand out{};
    assert(queue.popNext(0, out));
    assert(out.command_id == 2);
}

void test_event_queue_bounds()
{
    sys::runtime::FixedEventQueue<1> events;
    sys::runtime::RuntimeEvent event{};
    event.event_id = 1;
    event.kind = sys::runtime::RuntimeEventKind::MapTileReady;

    assert(events.enqueue(event));
    assert(!events.enqueue(event));
    assert(events.size() == 1);
    sys::runtime::RuntimeEvent out{};
    assert(events.pop(out));
    assert(out.event_id == 1);
}

void test_runtime_facade_submit_tick()
{
    sys::runtime::FixedCommandQueue<4> commands;
    sys::runtime::FixedEventSink<4> events;
    sys::runtime::RuntimeState state;
    sys::runtime::DefaultRuntimePolicyStrategy policy;
    FakeWorker worker;
    sys::runtime::RuntimeFacade facade(commands, worker, events, state, policy);

    sys::runtime::RuntimeIntent intent{};
    intent.kind = sys::runtime::RuntimeCommandKind::PersistenceSave;
    intent.origin = 7;
    intent.generation = 11;
    intent.dedupe_key = 99;
    intent.submitted_at_ms = 100;
    intent.priority_hint = sys::runtime::RuntimePriority::Background;
    intent.cancel_policy = sys::runtime::RuntimeCancelPolicy::CancelByDedupeKey;

    assert(facade.submit(intent));
    assert(state.status == sys::runtime::RuntimeStatus::Queued);
    assert(commands.size() == 1);
    assert(events.size() == 1);
    assert(facade.drainEvents() == 1);
    assert(facade.drainEvents() == 0);

    sys::runtime::RuntimeEvent queued{};
    assert(events.pop(queued));
    assert(queued.kind == sys::runtime::RuntimeEventKind::CommandQueued);
    assert(queued.command_id == 1);

    facade.tick(120);
    assert(worker.submitted);
    assert(worker.last_tick_ms == 120);
    assert(worker.last_command.kind == intent.kind);
    assert(worker.last_command.origin == intent.origin);
    assert(worker.last_command.generation == intent.generation);
    assert(worker.last_command.dedupe_key == intent.dedupe_key);
    assert(worker.last_command.priority == intent.priority_hint);
    assert(state.status == sys::runtime::RuntimeStatus::Running);

    sys::runtime::RuntimeEvent started{};
    assert(events.pop(started));
    assert(started.kind == sys::runtime::RuntimeEventKind::CommandStarted);
    assert(started.command_id == worker.last_command.command_id);
    assert(facade.drainEvents() == 1);
}

void test_runtime_facade_dedupe_cancel_policy()
{
    sys::runtime::FixedCommandQueue<4> commands;
    sys::runtime::FixedEventSink<4> events;
    sys::runtime::RuntimeState state;
    sys::runtime::DefaultRuntimePolicyStrategy policy;
    FakeWorker worker;
    sys::runtime::RuntimeFacade facade(commands, worker, events, state, policy);

    sys::runtime::RuntimeIntent first{};
    first.kind = sys::runtime::RuntimeCommandKind::PersistenceSave;
    first.dedupe_key = 44;
    first.cancel_policy = sys::runtime::RuntimeCancelPolicy::CancelByDedupeKey;
    first.submitted_at_ms = 1;

    sys::runtime::RuntimeIntent second = first;
    second.submitted_at_ms = 2;

    assert(facade.submit(first));
    assert(facade.submit(second));
    assert(commands.size() == 1);

    facade.tick(10);
    assert(worker.submitted);
    assert(worker.last_command.command_id == 2);
}

void test_event_to_ui_effect_bridge()
{
    sys::runtime::FixedEventSink<2> events;
    FakeUiEffectSink effects;
    sys::runtime::RuntimeEventUiEffectBridge bridge(events, effects);

    sys::runtime::RuntimeEvent event{};
    event.event_id = 7;
    event.kind = sys::runtime::RuntimeEventKind::PersistenceFailed;
    event.command_id = 3;
    event.error = -9;

    assert(bridge.publish(event));
    assert(effects.applied);
    assert(effects.last_effect.kind == event.kind);
    assert(effects.last_effect.event_id == event.event_id);
    assert(effects.last_effect.command_id == event.command_id);
    assert(effects.last_effect.error == event.error);

    sys::runtime::RuntimeEvent out{};
    assert(events.pop(out));
    assert(out.event_id == event.event_id);
}

void test_scoped_bus_access_token_releases_on_destruction()
{
    FakeBusArbiter arbiter;

    sys::runtime::BusAcquireRequest request{};
    request.resource = 8;
    request.command_id = 12;
    request.policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;

    {
        sys::runtime::ScopedBusAccessToken scope(arbiter, request);
        assert(scope.acquired());
        assert(static_cast<bool>(scope));
        assert(scope.status() == sys::runtime::BusAcquireStatus::Acquired);
        assert(scope.token().owner == 12);
        assert(scope.diagnostics().owner == 77);
        assert(arbiter.acquire_count == 1);
        assert(arbiter.release_count == 0);
    }

    assert(arbiter.release_count == 1);
}

void test_scoped_bus_access_token_release_is_idempotent()
{
    FakeBusArbiter arbiter;

    sys::runtime::BusAcquireRequest request{};
    request.policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;

    sys::runtime::ScopedBusAccessToken scope(arbiter, request);
    assert(scope.acquired());
    scope.release();
    scope.release();
    assert(!scope.acquired());
    assert(arbiter.release_count == 1);
}

void test_scoped_bus_access_token_move_transfers_release()
{
    FakeBusArbiter arbiter;

    sys::runtime::BusAcquireRequest request{};
    request.policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;

    {
        sys::runtime::ScopedBusAccessToken original(arbiter, request);
        assert(original.acquired());
        {
            sys::runtime::ScopedBusAccessToken moved(std::move(original));
            assert(!original.acquired());
            assert(moved.acquired());
            assert(arbiter.release_count == 0);
        }
        assert(arbiter.release_count == 1);
    }

    assert(arbiter.release_count == 1);
}

void test_scoped_bus_access_token_does_not_release_failed_acquire()
{
    FakeBusArbiter arbiter;
    arbiter.acquire_ok = false;

    sys::runtime::BusAcquireRequest request{};
    request.policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;

    {
        sys::runtime::ScopedBusAccessToken scope(arbiter, request);
        assert(!scope.acquired());
        assert(scope.status() == sys::runtime::BusAcquireStatus::TimedOut);
    }

    assert(arbiter.release_count == 0);
}

} // namespace

int main()
{
    test_priority_pop_order();
    test_dedupe_replace();
    test_cancel_generation();
    test_event_queue_bounds();
    test_runtime_facade_submit_tick();
    test_runtime_facade_dedupe_cancel_policy();
    test_event_to_ui_effect_bridge();
    test_scoped_bus_access_token_releases_on_destruction();
    test_scoped_bus_access_token_release_is_idempotent();
    test_scoped_bus_access_token_move_transfers_release();
    test_scoped_bus_access_token_does_not_release_failed_acquire();
    return 0;
}
