#pragma once

#include "sys/runtime_async.h"

#include <cstddef>
#include <cstdint>

namespace gps
{
namespace runtime
{

struct TrackPoint
{
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    uint32_t timestamp_ms = 0;
    bool has_altitude = false;
};

enum class TrackStorageFormat : uint8_t
{
    Gpx = 0,
    Csv = 1,
    Binary = 2,
};

enum class TrackCommandKind : uint8_t
{
    StartNewTrack,
    StopTrack,
    AppendPoint,
    Flush,
    ListTracks,
};

enum class TrackEventKind : uint8_t
{
    Started,
    Stopped,
    PointBuffered,
    ResourceBusy,
    FlushSucceeded,
    FlushFailed,
    ListReady,
    Failed,
};

enum class TrackRecorderStatus : uint8_t
{
    Idle,
    Starting,
    Recording,
    Flushing,
    Stopping,
    Stopped,
    Error,
    Recovering,
};

struct TrackPointBatch
{
    const TrackPoint* points = nullptr;
    std::size_t count = 0;
};

struct TrackStorageDescriptor
{
    uint32_t track_id = 0;
    const char* path = nullptr;
    TrackStorageFormat format = TrackStorageFormat::Gpx;
    const char* active_state_path = nullptr;
    bool manual_recording = false;
    bool auto_recording = false;
    bool persist_active_state = true;
    bool append_footer_on_close = true;
};

struct TrackCommand
{
    uint32_t command_id = 0;
    TrackCommandKind kind = TrackCommandKind::AppendPoint;
    TrackStorageDescriptor storage{};
    TrackPointBatch point_batch{};
    uint32_t deadline_ms = 0;
    uint32_t created_at_ms = 0;
};

struct TrackEvent
{
    TrackEventKind kind = TrackEventKind::Failed;
    uint32_t command_id = 0;
    uint32_t track_id = 0;
    TrackStorageDescriptor storage{};
    uint32_t timestamp_ms = 0;
    int32_t error = 0;
};

class TrackFlushPolicy
{
  public:
    virtual ~TrackFlushPolicy() = default;

    virtual bool shouldFlush(std::size_t buffer_size, uint32_t now_ms) const = 0;
    virtual bool isCritical(const TrackCommand& command) const = 0;
};

class DefaultTrackFlushPolicy : public TrackFlushPolicy
{
  public:
    bool shouldFlush(std::size_t buffer_size, uint32_t now_ms) const override
    {
        (void)now_ms;
        return buffer_size >= 8;
    }

    bool isCritical(const TrackCommand& command) const override
    {
        return command.kind == TrackCommandKind::StopTrack ||
               command.kind == TrackCommandKind::Flush;
    }
};

template <std::size_t N>
class TrackPointBuffer
{
  public:
    bool append(const TrackPoint& point)
    {
        if (count_ >= N)
        {
            return false;
        }
        points_[count_++] = point;
        return true;
    }

    TrackPointBatch takeBatch(const TrackFlushPolicy& policy, uint32_t now_ms)
    {
        if (!policy.shouldFlush(count_, now_ms))
        {
            return {};
        }
        batch_count_ = count_;
        for (std::size_t i = 0; i < count_; ++i)
        {
            batch_[i] = points_[i];
        }
        count_ = 0;
        return TrackPointBatch{batch_, batch_count_};
    }

    TrackPointBatch takeAll()
    {
        batch_count_ = count_;
        for (std::size_t i = 0; i < count_; ++i)
        {
            batch_[i] = points_[i];
        }
        count_ = 0;
        return TrackPointBatch{batch_, batch_count_};
    }

    std::size_t size() const
    {
        return count_;
    }

  private:
    TrackPoint points_[N]{};
    TrackPoint batch_[N]{};
    std::size_t count_ = 0;
    std::size_t batch_count_ = 0;
};

class TrackStateMachine
{
  public:
    TrackRecorderStatus state() const
    {
        return state_;
    }

    void transition(const TrackEvent& event)
    {
        switch (event.kind)
        {
        case TrackEventKind::Started:
            state_ = TrackRecorderStatus::Recording;
            break;
        case TrackEventKind::Stopped:
            state_ = TrackRecorderStatus::Stopped;
            break;
        case TrackEventKind::FlushSucceeded:
            state_ = TrackRecorderStatus::Recording;
            break;
        case TrackEventKind::ResourceBusy:
            state_ = TrackRecorderStatus::Recovering;
            break;
        case TrackEventKind::FlushFailed:
        case TrackEventKind::Failed:
            state_ = TrackRecorderStatus::Error;
            break;
        default:
            break;
        }
        last_error_ = event.error;
    }

    void setState(TrackRecorderStatus state)
    {
        state_ = state;
    }

    int32_t lastError() const
    {
        return last_error_;
    }

  private:
    TrackRecorderStatus state_ = TrackRecorderStatus::Idle;
    int32_t last_error_ = 0;
};

class ITrackFileAdapter
{
  public:
    virtual ~ITrackFileAdapter() = default;

    virtual bool open(const TrackStorageDescriptor& storage) = 0;
    virtual bool append(const TrackStorageDescriptor& storage,
                        const TrackPoint* points,
                        std::size_t count) = 0;
    virtual bool flush(const TrackStorageDescriptor& storage) = 0;
    virtual bool close(const TrackStorageDescriptor& storage) = 0;
    virtual std::size_t list(TrackStorageDescriptor* tracks, std::size_t capacity) = 0;
};

class ITrackEventSink
{
  public:
    virtual ~ITrackEventSink() = default;

    virtual bool publish(const TrackEvent& event) = 0;
};

class TrackStorageWorker
{
  public:
    TrackStorageWorker(ITrackFileAdapter& files,
                       sys::runtime::IBusArbiter& bus,
                       ITrackEventSink& events,
                       TrackFlushPolicy& policy)
        : files_(files), bus_(bus), events_(events), policy_(policy)
    {
    }

    bool submit(const TrackCommand& command)
    {
        if (pending_)
        {
            return false;
        }
        pending_command_ = command;
        pending_ = true;
        return true;
    }

    void tick(uint32_t now_ms)
    {
        if (!pending_)
        {
            return;
        }
        if (static_cast<int32_t>(retry_not_before_ms_ - now_ms) > 0)
        {
            return;
        }

        sys::runtime::BusAcquireRequest request{};
        request.command_id = pending_command_.command_id;
        request.deadline_ms = pending_command_.deadline_ms;
        request.policy = policy_.isCritical(pending_command_)
                             ? sys::runtime::BusAccessPolicy::DurableCommit
                             : sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
        const sys::runtime::BusAcquireResult acquire = bus_.acquire(request);
        if (acquire.status != sys::runtime::BusAcquireStatus::Acquired)
        {
            if (expired(now_ms))
            {
                publish(acquireFailureKind(), now_ms, -10);
                pending_ = false;
                retry_not_before_ms_ = 0;
                busy_event_published_ = false;
                return;
            }
            publishResourceBusy(now_ms, acquire.status);
            retry_not_before_ms_ = now_ms + kBusyRetryDelayMs;
            return;
        }

        retry_not_before_ms_ = 0;
        busy_event_published_ = false;

        bool ok = false;
        switch (pending_command_.kind)
        {
        case TrackCommandKind::StartNewTrack:
            ok = files_.open(pending_command_.storage);
            publish(ok ? TrackEventKind::Started : TrackEventKind::Failed,
                    now_ms,
                    ok ? 0 : -11);
            break;
        case TrackCommandKind::StopTrack:
            ok = appendBatchIfAny() && files_.flush(pending_command_.storage) &&
                 files_.close(pending_command_.storage);
            publish(ok ? TrackEventKind::Stopped : TrackEventKind::Failed,
                    now_ms,
                    ok ? 0 : -12);
            break;
        case TrackCommandKind::AppendPoint:
        case TrackCommandKind::Flush:
            ok = appendBatchIfAny();
            if (ok)
            {
                ok = files_.flush(pending_command_.storage);
            }
            publish(ok ? TrackEventKind::FlushSucceeded : TrackEventKind::FlushFailed,
                    now_ms,
                    ok ? 0 : -13);
            break;
        case TrackCommandKind::ListTracks:
        {
            TrackStorageDescriptor scratch[1]{};
            (void)files_.list(scratch, 0);
            publish(TrackEventKind::ListReady, now_ms, 0);
            ok = true;
            break;
        }
        }

        bus_.release(acquire.token);
        pending_ = false;
    }

    bool busy() const
    {
        return pending_;
    }

  private:
    static constexpr uint32_t kBusyRetryDelayMs = 25;
    static constexpr uint32_t kBusyEventIntervalMs = 250;

    bool appendBatchIfAny()
    {
        if (pending_command_.point_batch.count == 0)
        {
            return true;
        }
        return files_.append(pending_command_.storage,
                             pending_command_.point_batch.points,
                             pending_command_.point_batch.count);
    }

    bool expired(uint32_t now_ms) const
    {
        return pending_command_.deadline_ms != 0 &&
               static_cast<int32_t>(pending_command_.deadline_ms - now_ms) <= 0;
    }

    TrackEventKind acquireFailureKind() const
    {
        switch (pending_command_.kind)
        {
        case TrackCommandKind::AppendPoint:
        case TrackCommandKind::Flush:
            return TrackEventKind::FlushFailed;
        default:
            return TrackEventKind::Failed;
        }
    }

    void publishResourceBusy(uint32_t now_ms, sys::runtime::BusAcquireStatus status)
    {
        if (busy_event_published_ &&
            static_cast<uint32_t>(now_ms - last_busy_event_ms_) < kBusyEventIntervalMs)
        {
            return;
        }
        publish(TrackEventKind::ResourceBusy, now_ms, static_cast<int32_t>(status));
        busy_event_published_ = true;
        last_busy_event_ms_ = now_ms;
    }

    void publish(TrackEventKind kind, uint32_t now_ms, int32_t error)
    {
        TrackEvent event{};
        event.kind = kind;
        event.command_id = pending_command_.command_id;
        event.track_id = pending_command_.storage.track_id;
        event.storage = pending_command_.storage;
        event.timestamp_ms = now_ms;
        event.error = error;
        (void)events_.publish(event);
    }

    ITrackFileAdapter& files_;
    sys::runtime::IBusArbiter& bus_;
    ITrackEventSink& events_;
    TrackFlushPolicy& policy_;
    TrackCommand pending_command_{};
    bool pending_ = false;
    uint32_t retry_not_before_ms_ = 0;
    uint32_t last_busy_event_ms_ = 0;
    bool busy_event_published_ = false;
};

template <std::size_t N>
class TrackRuntime
{
  public:
    TrackRuntime(TrackPointBuffer<N>& points,
                 TrackStateMachine& states,
                 TrackFlushPolicy& policy,
                 TrackStorageWorker& worker,
                 ITrackEventSink& events)
        : points_(points), states_(states), policy_(policy), worker_(worker), events_(events)
    {
    }

    bool startNewTrack(uint32_t track_id, uint32_t now_ms)
    {
        return startNewTrack(makeStorage(track_id), now_ms);
    }

    bool startNewTrack(const TrackStorageDescriptor& storage, uint32_t now_ms)
    {
        states_.setState(TrackRecorderStatus::Starting);
        return submit(TrackCommandKind::StartNewTrack, storage, {}, now_ms);
    }

    bool stopTrack(uint32_t now_ms)
    {
        if (worker_.busy())
        {
            return false;
        }
        states_.setState(TrackRecorderStatus::Stopping);
        return submit(TrackCommandKind::StopTrack, active_storage_, points_.takeAll(), now_ms);
    }

    bool appendPoint(const TrackPoint& point, uint32_t now_ms)
    {
        if (!points_.append(point))
        {
            publish(TrackEventKind::Failed, now_ms, -20);
            return false;
        }
        publish(TrackEventKind::PointBuffered, now_ms, 0);
        return submitReadyBatch(now_ms);
    }

    bool listTracks(uint32_t now_ms)
    {
        return submit(TrackCommandKind::ListTracks, active_storage_, {}, now_ms);
    }

    void tick(uint32_t now_ms)
    {
        worker_.tick(now_ms);
        (void)submitReadyBatch(now_ms);
    }

    void handle(const TrackEvent& event)
    {
        states_.transition(event);
        last_event_ = event;
        if (event.kind == TrackEventKind::Started)
        {
            active_storage_ = event.storage;
        }
    }

    TrackEvent lastEvent() const
    {
        return last_event_;
    }

  private:
    static TrackStorageDescriptor makeStorage(uint32_t track_id)
    {
        TrackStorageDescriptor storage{};
        storage.track_id = track_id;
        return storage;
    }

    bool submit(TrackCommandKind kind,
                const TrackStorageDescriptor& storage,
                TrackPointBatch batch,
                uint32_t now_ms)
    {
        TrackCommand command{};
        command.command_id = next_command_id_++;
        command.kind = kind;
        command.storage = storage;
        command.point_batch = batch;
        command.created_at_ms = now_ms;
        if (kind == TrackCommandKind::StartNewTrack)
        {
            active_storage_ = storage;
        }
        return worker_.submit(command);
    }

    bool submitReadyBatch(uint32_t now_ms)
    {
        if (worker_.busy())
        {
            return true;
        }
        TrackPointBatch batch = points_.takeBatch(policy_, now_ms);
        if (batch.count == 0)
        {
            return true;
        }
        return submit(TrackCommandKind::AppendPoint, active_storage_, batch, now_ms);
    }

    void publish(TrackEventKind kind, uint32_t now_ms, int32_t error)
    {
        TrackEvent event{};
        event.kind = kind;
        event.track_id = active_storage_.track_id;
        event.storage = active_storage_;
        event.timestamp_ms = now_ms;
        event.error = error;
        last_event_ = event;
        (void)events_.publish(event);
    }

    TrackPointBuffer<N>& points_;
    TrackStateMachine& states_;
    TrackFlushPolicy& policy_;
    TrackStorageWorker& worker_;
    ITrackEventSink& events_;
    TrackEvent last_event_{};
    TrackStorageDescriptor active_storage_{};
    uint32_t next_command_id_ = 1;
};

} // namespace runtime
} // namespace gps
