#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"

#include <cassert>
#include <cstring>

namespace
{

ui::map_tiles::MapTileRef make_tile(uint32_t x)
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 12;
    ref.x = x;
    ref.y = x + 1;
    return ref;
}

class FakeCommandSink final : public ui::map_tiles::IMapTileCommandSink
{
  public:
    ui::map_tiles::LoadTileCommand commands[8]{};
    std::size_t count = 0;
    std::size_t cancelled = 0;

    bool enqueue(const ui::map_tiles::LoadTileCommand& command) override
    {
        if (count >= 8)
        {
            return false;
        }
        commands[count++] = command;
        return true;
    }

    std::size_t cancelGeneration(uint32_t generation) override
    {
        std::size_t removed = 0;
        ui::map_tiles::LoadTileCommand kept[8]{};
        std::size_t kept_count = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (commands[i].runtime.generation == generation)
            {
                ++removed;
                continue;
            }
            kept[kept_count++] = commands[i];
        }
        for (std::size_t i = 0; i < kept_count; ++i)
        {
            commands[i] = kept[i];
        }
        count = kept_count;
        cancelled += removed;
        return removed;
    }
};

class FakeBusArbiter final : public sys::runtime::IBusArbiter
{
  public:
    sys::runtime::BusAcquireStatus next_status = sys::runtime::BusAcquireStatus::Acquired;
    sys::runtime::BusAccessPolicy last_policy = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    uint32_t last_resource = 0;
    int acquire_count = 0;
    int release_count = 0;

    sys::runtime::BusAcquireResult acquire(const sys::runtime::BusAcquireRequest& request) override
    {
        ++acquire_count;
        last_policy = request.policy;
        last_resource = request.resource;
        sys::runtime::BusAcquireResult result{};
        result.status = next_status;
        result.token.valid = next_status == sys::runtime::BusAcquireStatus::Acquired;
        result.token.resource = request.resource;
        result.token.owner = request.command_id;
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
};

class FakeBackend final : public ui::map_tiles::IMapTileWorkerBackend
{
  public:
    bool available = true;
    bool read_ok = true;
    int lookup_count = 0;
    int read_count = 0;

    ui::map_tiles::MapTileLookupResult lookup(const ui::map_tiles::MapTileRef& ref) override
    {
        ++lookup_count;
        (void)ref;
        ui::map_tiles::MapTileLookupResult result{};
        result.status = available ? ui::map_tiles::MapTileStatus::Available
                                  : ui::map_tiles::MapTileStatus::Missing;
        result.format = ui::map_tiles::MapTileFormat::Png;
        return result;
    }

    bool read(const ui::map_tiles::MapTileRef& ref,
              uint8_t* buffer,
              std::size_t capacity,
              std::size_t& out_size,
              ui::map_tiles::MapTileFormat& out_format) override
    {
        ++read_count;
        (void)ref;
        out_size = 0;
        out_format = ui::map_tiles::MapTileFormat::Png;
        if (!available || !read_ok || !buffer || capacity < 3)
        {
            return false;
        }
        buffer[0] = 1;
        buffer[1] = 2;
        buffer[2] = 3;
        out_size = 3;
        return true;
    }
};

class FakeEventSink final : public ui::map_tiles::IMapTileEventSink
{
  public:
    ui::map_tiles::MapTileAsyncEvent events[4]{};
    std::size_t count = 0;

    bool publish(const ui::map_tiles::MapTileAsyncEvent& event) override
    {
        if (count >= 4)
        {
            return false;
        }
        events[count++] = event;
        return true;
    }
};

class FakePolicy final : public sys::runtime::RuntimePolicyStrategy
{
  public:
    sys::runtime::RuntimePriority selectPriority(
        const sys::runtime::RuntimeIntent& intent) const override
    {
        (void)intent;
        return sys::runtime::RuntimePriority::Realtime;
    }

    sys::runtime::BusAccessPolicy selectBusPolicy(
        const sys::runtime::RuntimeCommand& command) const override
    {
        (void)command;
        return sys::runtime::BusAccessPolicy::RecoveryExclusive;
    }

    sys::runtime::RuntimeRetryDecision selectRetry(
        const sys::runtime::RuntimeCommand& command,
        const sys::runtime::PlatformStorageResult& result) const override
    {
        (void)command;
        (void)result;
        return {};
    }
};

void test_generation_cancels_old_commands()
{
    FakeCommandSink sink;
    ui::map_tiles::MapTileAsyncRuntime runtime(sink);

    ui::map_tiles::MapViewportPlan first{};
    first.generation = 1;
    first.tile_count = 1;
    first.tiles[0] = make_tile(10);
    assert(runtime.requestVisibleTiles(first, 100) == 1);
    assert(sink.count == 1);

    ui::map_tiles::MapViewportPlan second{};
    second.generation = 2;
    second.tile_count = 1;
    second.tiles[0] = make_tile(20);
    assert(runtime.requestVisibleTiles(second, 120) == 1);
    assert(sink.cancelled == 1);
    assert(sink.count == 1);
    assert(sink.commands[0].runtime.generation == 2);
}

void test_stale_event_is_ignored()
{
    FakeCommandSink sink;
    ui::map_tiles::MapTileAsyncRuntime runtime(sink);
    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 2;
    plan.tile_count = 1;
    plan.tiles[0] = make_tile(20);
    assert(runtime.requestVisibleTiles(plan, 100) == 1);

    ui::map_tiles::MapTileRenderQueue render_queue;
    ui::map_tiles::MapTileAsyncEvent stale{};
    stale.kind = ui::map_tiles::MapTileAsyncEventKind::Ready;
    stale.generation = 1;
    stale.tile = make_tile(10);
    assert(!runtime.handleEvent(stale, render_queue));
    assert(render_queue.size() == 0);

    ui::map_tiles::MapTileAsyncEvent current = stale;
    current.generation = 2;
    current.tile = make_tile(20);
    assert(runtime.handleEvent(current, render_queue));
    assert(render_queue.size() == 1);
    assert(render_queue.items()[0].state == ui::map_tiles::MapTileRenderState::Ready);
}

void test_map_tile_runtime_does_not_transition_state_for_stale_event()
{
    FakeCommandSink sink;
    ui::map_tiles::MapTileAsyncRuntime async_runtime(sink);
    ui::map_tiles::MapTileStateMachine state_machine;
    ui::map_tiles::MapTileRuntime runtime(async_runtime, state_machine);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 7;
    plan.tile_count = 1;
    plan.tiles[0] = make_tile(70);
    assert(runtime.requestVisibleTiles(plan, 700) == 1);

    ui::map_tiles::MapTileRenderQueue render_queue;
    ui::map_tiles::MapTileAsyncEvent stale{};
    stale.kind = ui::map_tiles::MapTileAsyncEventKind::Ready;
    stale.generation = 6;
    stale.tile = make_tile(60);
    assert(!runtime.handle(stale, render_queue));
    assert(runtime.snapshot().active_generation == 0);
    assert(runtime.snapshot().ready_count == 0);
    assert(render_queue.size() == 0);

    ui::map_tiles::MapTileAsyncEvent current = stale;
    current.generation = 7;
    current.tile = make_tile(70);
    assert(runtime.handle(current, render_queue));
    assert(runtime.snapshot().active_generation == 7);
    assert(runtime.snapshot().ready_count == 1);
    assert(render_queue.size() == 1);
}

void test_interactive_plan_uses_interactive_priority()
{
    FakeCommandSink sink;
    ui::map_tiles::MapTileAsyncRuntime runtime(sink);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 8;
    plan.interaction_mode = ui::map_tiles::MapTileInteractionMode::InteractiveDrag;
    plan.tile_count = 1;
    plan.tiles[0] = make_tile(80);
    assert(runtime.requestVisibleTiles(plan, 800) == 1);
    assert(sink.count == 1);
    assert(sink.commands[0].runtime.priority == sys::runtime::RuntimePriority::Interactive);
}

void test_tile_dedupe_key_uses_full_tile_identity()
{
    FakeCommandSink sink;
    ui::map_tiles::MapTileAsyncRuntime runtime(sink);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 9;
    plan.tile_count = 3;
    plan.tiles[0] = make_tile(4096);
    plan.tiles[1] = plan.tiles[0];
    plan.tiles[1].x += 4096;
    plan.tiles[1].y += 4096;
    plan.tiles[2] = plan.tiles[0];
    plan.tiles[2].layer = ui::map_tiles::MapTileLayer::Terrain;

    assert(runtime.requestVisibleTiles(plan, 900) == 3);
    assert(sink.count == 3);
    assert(sink.commands[0].runtime.dedupe_key != 0);
    assert(sink.commands[1].runtime.dedupe_key != 0);
    assert(sink.commands[2].runtime.dedupe_key != 0);
    assert(sink.commands[0].runtime.dedupe_key != sink.commands[1].runtime.dedupe_key);
    assert(sink.commands[0].runtime.dedupe_key != sink.commands[2].runtime.dedupe_key);
    assert(sink.commands[1].runtime.dedupe_key != sink.commands[2].runtime.dedupe_key);
}

void test_worker_busy_does_not_read_storage()
{
    FakeBackend backend;
    FakeBusArbiter bus;
    FakeEventSink events;
    uint8_t scratch[8]{};
    ui::map_tiles::MapTileWorker worker(backend, bus, events, scratch, sizeof(scratch));

    ui::map_tiles::LoadTileCommand command{};
    command.runtime.command_id = 7;
    command.runtime.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    command.runtime.generation = 3;
    command.runtime.priority = sys::runtime::RuntimePriority::Interactive;
    command.tile = make_tile(30);

    bus.next_status = sys::runtime::BusAcquireStatus::Busy;
    assert(!worker.execute(command, 200));
    assert(bus.acquire_count == 1);
    assert(bus.release_count == 0);
    assert(backend.lookup_count == 0);
    assert(backend.read_count == 0);
    assert(events.count == 1);
    assert(events.events[0].kind == ui::map_tiles::MapTileAsyncEventKind::ResourceBusy);
    assert(bus.last_policy == sys::runtime::BusAccessPolicy::DisplayFrameCritical);
}

void test_worker_success_publishes_ready()
{
    FakeBackend backend;
    FakeBusArbiter bus;
    FakeEventSink events;
    uint8_t scratch[8]{};
    ui::map_tiles::MapTileWorker worker(backend, bus, events, scratch, sizeof(scratch));

    ui::map_tiles::LoadTileCommand command{};
    command.runtime.command_id = 9;
    command.runtime.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    command.runtime.generation = 4;
    command.runtime.priority = sys::runtime::RuntimePriority::Normal;
    command.runtime.origin = 42;
    command.tile = make_tile(40);

    assert(worker.execute(command, 300));
    assert(bus.acquire_count == 1);
    assert(bus.release_count == 1);
    assert(bus.last_resource == 42);
    assert(backend.lookup_count == 0);
    assert(backend.read_count == 1);
    assert(events.count == 1);
    assert(events.events[0].kind == ui::map_tiles::MapTileAsyncEventKind::Ready);
    assert(events.events[0].payload_size == 3);
    assert(events.events[0].payload.data == scratch);
    assert(events.events[0].payload.size == 3);
    assert(events.events[0].payload.format == ui::map_tiles::MapTileFormat::Png);
}

void test_worker_missing_reads_once_without_lookup_probe()
{
    FakeBackend backend;
    backend.available = false;
    FakeBusArbiter bus;
    FakeEventSink events;
    uint8_t scratch[8]{};
    ui::map_tiles::MapTileWorker worker(backend, bus, events, scratch, sizeof(scratch));

    ui::map_tiles::LoadTileCommand command{};
    command.runtime.command_id = 11;
    command.runtime.kind = sys::runtime::RuntimeCommandKind::MapTileLoad;
    command.runtime.generation = 4;
    command.runtime.priority = sys::runtime::RuntimePriority::Normal;
    command.tile = make_tile(41);

    assert(!worker.execute(command, 310));
    assert(bus.acquire_count == 1);
    assert(bus.release_count == 1);
    assert(backend.lookup_count == 0);
    assert(backend.read_count == 1);
    assert(events.count == 1);
    assert(events.events[0].kind == ui::map_tiles::MapTileAsyncEventKind::Failed);
    assert(events.events[0].payload.data == nullptr);
    assert(events.events[0].payload_size == 0);
}

void test_runtime_and_worker_use_policy_strategy()
{
    FakeCommandSink sink;
    FakePolicy policy;
    ui::map_tiles::MapTileAsyncRuntime runtime(sink, &policy);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 5;
    plan.tile_count = 1;
    plan.tiles[0] = make_tile(50);
    assert(runtime.requestVisibleTiles(plan, 500) == 1);
    assert(sink.commands[0].runtime.priority == sys::runtime::RuntimePriority::Realtime);

    FakeBackend backend;
    FakeBusArbiter bus;
    FakeEventSink events;
    uint8_t scratch[8]{};
    ui::map_tiles::MapTileWorker worker(backend, bus, events, scratch, sizeof(scratch), &policy);
    assert(worker.execute(sink.commands[0], 510));
    assert(bus.last_policy == sys::runtime::BusAccessPolicy::RecoveryExclusive);
}

} // namespace

int main()
{
    test_generation_cancels_old_commands();
    test_stale_event_is_ignored();
    test_map_tile_runtime_does_not_transition_state_for_stale_event();
    test_interactive_plan_uses_interactive_priority();
    test_tile_dedupe_key_uses_full_tile_identity();
    test_worker_busy_does_not_read_storage();
    test_worker_success_publishes_ready();
    test_worker_missing_reads_once_without_lookup_probe();
    test_runtime_and_worker_use_policy_strategy();
    return 0;
}
