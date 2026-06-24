#include "sys/storage_event_runtime.h"

#include <cassert>
#include <cstring>

using sys::runtime::LatestSnapshotStorageRuntime;
using sys::runtime::StorageWorkItem;
using sys::runtime::StorageWorkState;

namespace
{

class FakeStorageAdapter final : public sys::runtime::IPlatformStorageAdapter
{
  public:
    sys::runtime::PlatformStorageResult read(
        const sys::runtime::PlatformStorageReadRequest& request) override
    {
        (void)request;
        return {};
    }

    sys::runtime::PlatformStorageResult write(
        const sys::runtime::PlatformStorageWriteRequest& request) override
    {
        writes += 1;
        last_path = request.path;
        last_len = request.len;
        sys::runtime::PlatformStorageResult result{};
        result.ok = write_ok;
        result.bytes = write_ok ? request.len : 0;
        result.error = write_ok ? 0 : -7;
        return result;
    }

    sys::runtime::PlatformStorageResult list(
        const sys::runtime::PlatformStorageListRequest& request) override
    {
        (void)request;
        return {};
    }

    sys::runtime::PlatformStorageResult flush(
        const sys::runtime::PlatformStorageFlushRequest& request) override
    {
        (void)request;
        sys::runtime::PlatformStorageResult result{};
        result.ok = true;
        return result;
    }

    bool write_ok = true;
    int writes = 0;
    const char* last_path = nullptr;
    std::size_t last_len = 0;
};

void burst_updates_coalesce_to_latest_snapshot()
{
    LatestSnapshotStorageRuntime<16> runtime;
    const uint8_t first[] = {1, 2, 3};
    const uint8_t second[] = {9, 8};

    assert(runtime.requestSave(42, first, sizeof(first)));
    assert(runtime.requestSave(42, second, sizeof(second)));

    StorageWorkItem work;
    assert(runtime.takeNext(work));
    assert(work.key == 42);
    assert(work.generation == 2);
    assert(work.len == sizeof(second));
    assert(std::memcmp(work.data, second, sizeof(second)) == 0);
}

void completion_ignores_stale_generation()
{
    LatestSnapshotStorageRuntime<16> runtime;
    const uint8_t first[] = {1};
    const uint8_t second[] = {2};

    assert(runtime.requestSave(1, first, sizeof(first)));
    StorageWorkItem work;
    assert(runtime.takeNext(work));
    assert(runtime.requestSave(1, second, sizeof(second)));

    runtime.complete(work.generation + 100, true);
    assert(runtime.busy());

    runtime.complete(work.generation, true);
    assert(runtime.pending());
    assert(runtime.state() == StorageWorkState::Pending);
}

void failed_save_is_retried_with_same_generation()
{
    LatestSnapshotStorageRuntime<16> runtime;
    const uint8_t blob[] = {4, 5, 6};

    assert(runtime.requestSave(7, blob, sizeof(blob)));
    StorageWorkItem first;
    assert(runtime.takeNext(first));
    runtime.complete(first.generation, false);

    assert(runtime.pending());
    assert(runtime.state() == StorageWorkState::FailedPendingRetry);

    StorageWorkItem retry;
    assert(runtime.takeNext(retry));
    assert(retry.generation == first.generation);
    assert(retry.len == sizeof(blob));
    assert(std::memcmp(retry.data, blob, sizeof(blob)) == 0);
}

void flush_pending_writes_through_storage_port_and_publishes_event()
{
    LatestSnapshotStorageRuntime<16> runtime;
    FakeStorageAdapter storage;
    sys::runtime::FixedEventSink<4> events;
    const uint8_t blob[] = {1, 3, 5, 7};

    assert(runtime.requestSave(12, blob, sizeof(blob)));
    assert(runtime.flushPending(storage, events, "/nodes.bin", 50));
    assert(storage.writes == 1);
    assert(storage.last_path != nullptr);
    assert(std::strcmp(storage.last_path, "/nodes.bin") == 0);
    assert(storage.last_len == sizeof(blob));
    assert(!runtime.pending());

    sys::runtime::RuntimeEvent event{};
    assert(events.pop(event));
    assert(event.kind == sys::runtime::RuntimeEventKind::PersistenceSaved);
    assert(event.timestamp_ms == 50);
}

void failed_flush_keeps_latest_snapshot_pending_for_retry()
{
    LatestSnapshotStorageRuntime<16> runtime;
    FakeStorageAdapter storage;
    storage.write_ok = false;
    sys::runtime::FixedEventSink<4> events;
    const uint8_t blob[] = {2, 4, 6};

    assert(runtime.requestSave(9, blob, sizeof(blob)));
    assert(!runtime.flushPending(storage, events, "/nodes.bin", 60));
    assert(runtime.pending());
    assert(runtime.state() == StorageWorkState::FailedPendingRetry);

    sys::runtime::RuntimeEvent event{};
    assert(events.pop(event));
    assert(event.kind == sys::runtime::RuntimeEventKind::PersistenceFailed);
    assert(event.error == -7);
}

} // namespace

int main()
{
    burst_updates_coalesce_to_latest_snapshot();
    completion_ignores_stale_generation();
    failed_save_is_retried_with_same_generation();
    flush_pending_writes_through_storage_port_and_publishes_event();
    failed_flush_keeps_latest_snapshot_pending_for_retry();
    return 0;
}
