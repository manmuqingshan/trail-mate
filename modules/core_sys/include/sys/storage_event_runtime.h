#pragma once

#include "sys/runtime_async.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sys::runtime
{

enum class StorageWorkKind : uint8_t
{
    SnapshotSave,
    LogAppend,
    SnapshotRead,
    DeleteBlob,
};

enum class StorageWorkState : uint8_t
{
    Idle,
    Pending,
    InFlight,
    FailedPendingRetry,
};

struct StorageWorkItem
{
    StorageWorkKind kind = StorageWorkKind::SnapshotSave;
    uint32_t key = 0;
    uint32_t generation = 0;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

template <size_t MaxBytes>
class LatestSnapshotStorageRuntime
{
  public:
    bool requestSave(uint32_t key, const uint8_t* data, size_t len)
    {
        if (len > MaxBytes || (len > 0 && data == nullptr))
        {
            return false;
        }
        key_ = key;
        len_ = len;
        if (len > 0)
        {
            std::memcpy(buffer_.data(), data, len);
        }
        ++generation_;
        pending_ = true;
        state_ = (state_ == StorageWorkState::InFlight)
                     ? StorageWorkState::InFlight
                     : StorageWorkState::Pending;
        return true;
    }

    bool takeNext(StorageWorkItem& out)
    {
        if (!pending_)
        {
            return false;
        }
        pending_ = false;
        in_flight_generation_ = generation_;
        state_ = StorageWorkState::InFlight;
        out.kind = StorageWorkKind::SnapshotSave;
        out.key = key_;
        out.generation = in_flight_generation_;
        out.data = buffer_.data();
        out.len = len_;
        return true;
    }

    void complete(uint32_t generation, bool ok)
    {
        if (generation != in_flight_generation_)
        {
            return;
        }
        if (ok)
        {
            state_ = pending_ ? StorageWorkState::Pending : StorageWorkState::Idle;
            last_completed_generation_ = generation;
            return;
        }
        pending_ = true;
        state_ = StorageWorkState::FailedPendingRetry;
    }

    bool flushPending(IPlatformStorageAdapter& storage,
                      IEventSink& events,
                      const char* path,
                      uint32_t now_ms)
    {
        StorageWorkItem work{};
        if (!takeNext(work))
        {
            return true;
        }

        PlatformStorageWriteRequest request{};
        request.command_id = work.generation;
        request.path = path;
        request.data = work.data;
        request.len = work.len;
        request.durable = true;
        const PlatformStorageResult result = storage.write(request);
        complete(work.generation, result.ok);

        RuntimeEvent event{};
        event.event_id = work.generation;
        event.kind = result.ok ? RuntimeEventKind::PersistenceSaved
                               : RuntimeEventKind::PersistenceFailed;
        event.command_id = work.generation;
        event.timestamp_ms = now_ms;
        event.generation = work.generation;
        event.error = result.error;
        (void)events.publish(event);
        return result.ok;
    }

    bool pending() const { return pending_; }
    bool busy() const { return state_ == StorageWorkState::InFlight; }
    StorageWorkState state() const { return state_; }
    uint32_t generation() const { return generation_; }
    uint32_t lastCompletedGeneration() const { return last_completed_generation_; }
    size_t len() const { return len_; }

  private:
    std::array<uint8_t, MaxBytes> buffer_{};
    size_t len_ = 0;
    uint32_t key_ = 0;
    uint32_t generation_ = 0;
    uint32_t in_flight_generation_ = 0;
    uint32_t last_completed_generation_ = 0;
    bool pending_ = false;
    StorageWorkState state_ = StorageWorkState::Idle;
};

} // namespace sys::runtime
