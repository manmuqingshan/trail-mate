#include "sys/runtime_async.h"

#include <cassert>

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

class FakeBusAdapter final : public sys::runtime::IBusAdapter
{
  public:
    bool acquire_ok = true;
    uint32_t now_ms = 100;
    uint32_t last_timeout_ms = 0;
    int acquire_count = 0;
    int release_count = 0;

    bool tryAcquire(uint32_t timeout_ms) override
    {
        last_timeout_ms = timeout_ms;
        ++acquire_count;
        now_ms += timeout_ms;
        return acquire_ok;
    }

    void release() override
    {
        ++release_count;
    }

    uint32_t nowMs() const override
    {
        return now_ms;
    }

    uint32_t owner() const override
    {
        return 77;
    }
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

void test_storage_bus_arbiter_uses_policy_timeout()
{
    FakeBusAdapter adapter;
    sys::runtime::DefaultBusPolicyStrategy policy;
    sys::runtime::StorageBusArbiter arbiter(adapter, policy);

    sys::runtime::BusAcquireRequest request{};
    request.resource = 3;
    request.command_id = 9;
    request.policy = sys::runtime::BusAccessPolicy::InteractiveWorkerBounded;

    const sys::runtime::BusAcquireResult result = arbiter.acquire(request);
    assert(result.status == sys::runtime::BusAcquireStatus::Acquired);
    assert(result.token.valid);
    assert(result.token.owner == 9);
    assert(adapter.last_timeout_ms == 2);
    assert(result.diagnostics.owner == 77);

    arbiter.release(result.token);
    assert(adapter.release_count == 1);
}

void test_map_tile_policy_is_display_frame_critical()
{
    FakeBusAdapter adapter;
    sys::runtime::DefaultBusPolicyStrategy policy;
    sys::runtime::StorageBusArbiter arbiter(adapter, policy);

    sys::runtime::RuntimeCommand command{};
    command.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    command.priority = sys::runtime::RuntimePriority::Normal;
    assert(policy.select(command) == sys::runtime::BusAccessPolicy::DisplayFrameCritical);

    sys::runtime::BusAcquireRequest request{};
    request.policy = policy.select(command);
    const sys::runtime::BusAcquireResult result = arbiter.acquire(request);
    assert(result.status == sys::runtime::BusAcquireStatus::Acquired);
    assert(adapter.last_timeout_ms == 0);
    arbiter.release(result.token);
}

void test_storage_bus_arbiter_reports_degraded_after_timeouts()
{
    FakeBusAdapter adapter;
    adapter.acquire_ok = false;
    sys::runtime::DefaultBusPolicyStrategy policy;
    sys::runtime::StorageBusArbiter arbiter(adapter, policy);

    sys::runtime::BusAcquireRequest request{};
    request.policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;

    assert(arbiter.acquire(request).status == sys::runtime::BusAcquireStatus::TimedOut);
    assert(arbiter.health().status == sys::runtime::StorageHealthStatus::Slow);
    assert(arbiter.acquire(request).status == sys::runtime::BusAcquireStatus::TimedOut);
    assert(arbiter.acquire(request).status == sys::runtime::BusAcquireStatus::TimedOut);
    assert(arbiter.health().status == sys::runtime::StorageHealthStatus::Degraded);
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
    test_storage_bus_arbiter_uses_policy_timeout();
    test_map_tile_policy_is_display_frame_critical();
    test_storage_bus_arbiter_reports_degraded_after_timeouts();
    return 0;
}
