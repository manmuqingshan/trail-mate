#include "gps/track_runtime.h"
#include "sys/feedback_runtime.h"
#include "sys/persistence_runtime.h"
#include "sys/runtime_harness.h"
#include "ui_map_runtime/map_tiles/map_tile_async_runtime.h"

#include <cassert>
#include <cstddef>

namespace
{

class MapCommandSink final : public ui::map_tiles::IMapTileCommandSink
{
  public:
    bool enqueue(const ui::map_tiles::LoadTileCommand& command) override
    {
        if (count >= 4)
        {
            return false;
        }
        commands[count++] = command;
        return true;
    }

    std::size_t cancelGeneration(uint32_t generation) override
    {
        std::size_t removed = 0;
        ui::map_tiles::LoadTileCommand kept[4]{};
        std::size_t kept_count = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (commands[i].runtime.generation == generation)
            {
                ++removed;
            }
            else
            {
                kept[kept_count++] = commands[i];
            }
        }
        for (std::size_t i = 0; i < kept_count; ++i)
        {
            commands[i] = kept[i];
        }
        count = kept_count;
        return removed;
    }

    ui::map_tiles::LoadTileCommand commands[4]{};
    std::size_t count = 0;
};

class MapUiSink final : public ui::map_tiles::IMapTileUiSink
{
  public:
    bool applyTile(const ui::map_tiles::MapTileEvent& event) override
    {
        last = event;
        ++count;
        return true;
    }

    ui::map_tiles::MapTileEvent last{};
    std::size_t count = 0;
};

class TrackEvents final : public gps::runtime::ITrackEventSink
{
  public:
    bool publish(const gps::runtime::TrackEvent& event) override
    {
        if (count >= 8)
        {
            return false;
        }
        events[count++] = event;
        return true;
    }

    gps::runtime::TrackEvent events[8]{};
    std::size_t count = 0;
};

class TrackFiles final : public gps::runtime::ITrackFileAdapter
{
  public:
    bool open(uint32_t track_id) override
    {
        active_track = track_id;
        ++open_count;
        return true;
    }

    bool append(uint32_t track_id,
                const gps::runtime::TrackPoint* points,
                std::size_t count) override
    {
        assert(track_id == active_track);
        assert(points != nullptr || count == 0);
        appended += count;
        return true;
    }

    bool flush(uint32_t track_id) override
    {
        assert(track_id == active_track);
        ++flush_count;
        return true;
    }

    bool close(uint32_t track_id) override
    {
        assert(track_id == active_track);
        ++close_count;
        return true;
    }

    std::size_t list(uint32_t* track_ids, std::size_t capacity) override
    {
        if (track_ids && capacity > 0)
        {
            track_ids[0] = active_track;
            return 1;
        }
        return 0;
    }

    uint32_t active_track = 0;
    std::size_t appended = 0;
    std::size_t open_count = 0;
    std::size_t flush_count = 0;
    std::size_t close_count = 0;
};

ui::map_tiles::MapTileRef tileRef()
{
    ui::map_tiles::MapTileRef ref{};
    ref.layer = ui::map_tiles::MapTileLayer::Osm;
    ref.z = 12;
    ref.x = 100;
    ref.y = 200;
    return ref;
}

void test_map_tile_runtime_contract()
{
    MapCommandSink commands;
    ui::map_tiles::MapTileAsyncRuntime async(commands);
    ui::map_tiles::MapTileStateMachine states;
    MapUiSink ui;
    ui::map_tiles::MapTileRuntime runtime(async, states, &ui);

    ui::map_tiles::MapViewportPlan plan{};
    plan.generation = 7;
    plan.tile_count = 1;
    plan.tiles[0] = tileRef();
    assert(runtime.requestVisibleTiles(plan, 10) == 1);
    assert(commands.count == 1);

    ui::map_tiles::MapTileEvent ready{};
    ready.kind = ui::map_tiles::MapTileEventKind::Ready;
    ready.generation = 7;
    ready.tile = tileRef();
    ui::map_tiles::MapTileRenderQueue render_queue;
    assert(runtime.handle(ready, render_queue));
    assert(ui.count == 1);
    assert(runtime.snapshot().ready_count == 1);
}

void test_persistence_runtime_contract()
{
    sys::runtime::RuntimeHarness harness;
    sys::runtime::DirtyStoreRegistry<4> registry;
    sys::runtime::DefaultPersistencePolicy policy;
    sys::runtime::PersistenceWorker worker(harness.storage(),
                                           harness.storage(),
                                           harness.bus(),
                                           harness.events(),
                                           policy);
    sys::runtime::PersistenceRuntime<4> runtime(registry,
                                                worker,
                                                harness.events(),
                                                policy);

    assert(runtime.markDirty("nodes", 0));
    runtime.tick(100);
    assert(harness.storage().writeCount() == 0);
    runtime.tick(150);
    assert(harness.storage().writeCount() == 1);
    assert(harness.bus().acquireCount() == 1);
    assert(harness.events().persistenceCount() >= 3);
}

void test_feedback_runtime_contract()
{
    sys::runtime::RuntimeHarness harness;
    sys::runtime::FeedbackQueue<4> queue;
    sys::runtime::DefaultFeedbackPolicy policy;
    sys::runtime::FeedbackRuntime<4> runtime(queue,
                                             policy,
                                             harness.feedbackPresenter(),
                                             harness.events());

    sys::runtime::NoticeIntent sent{};
    sent.category = sys::runtime::NoticeCategory::ChatDelivery;
    sent.severity = sys::runtime::NoticeSeverity::Success;
    sys::runtime::setNoticeMessage(sent, "Sent");
    sent.dedupe_key = 42;
    sent.created_at_ms = 10;
    assert(runtime.post(sent));
    assert(runtime.post(sent));
    assert(runtime.drainToUi(20) == 1);
    assert(harness.feedbackPresenter().captured() == 1);
    assert(harness.feedbackPresenter().capturedNotice(0).duration_ms == 1400);
}

void test_track_runtime_contract()
{
    sys::runtime::RuntimeHarness harness;
    TrackFiles files;
    TrackEvents events;
    gps::runtime::DefaultTrackFlushPolicy policy;
    gps::runtime::TrackStorageWorker worker(files, harness.bus(), events, policy);
    gps::runtime::TrackPointBuffer<8> points;
    gps::runtime::TrackStateMachine states;
    gps::runtime::TrackRuntime<8> runtime(points, states, policy, worker, events);

    assert(runtime.startNewTrack(123, 0));
    runtime.tick(1);
    assert(events.count == 1);
    runtime.handle(events.events[0]);
    assert(states.state() == gps::runtime::TrackRecorderStatus::Recording);

    gps::runtime::TrackPoint point{};
    point.latitude = 1.0;
    point.longitude = 2.0;
    for (int i = 0; i < 8; ++i)
    {
        point.timestamp_ms = static_cast<uint32_t>(i);
        assert(runtime.appendPoint(point, 10 + static_cast<uint32_t>(i)));
    }
    runtime.tick(30);
    assert(files.appended == 8);
    assert(files.flush_count == 1);
}

void test_runtime_harness_keeps_ui_drain_separate()
{
    sys::runtime::RuntimeHarness harness;
    harness.advance(5);
    harness.ui().assertNoBlockingCalls();
    assert(harness.drainUi() == 0);
}

} // namespace

int main()
{
    test_map_tile_runtime_contract();
    test_persistence_runtime_contract();
    test_feedback_runtime_contract();
    test_track_runtime_contract();
    test_runtime_harness_keeps_ui_drain_separate();
    return 0;
}
