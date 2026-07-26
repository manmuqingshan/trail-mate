#pragma once

#include "sys/feedback_runtime.h"
#include "sys/persistence_runtime.h"
#include "sys/runtime_async.h"
#include "sys/shared_spi_access.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace sys
{
namespace runtime
{

class FakeClock
{
  public:
    uint32_t now() const
    {
        return now_ms_;
    }

    void advance(uint32_t ms)
    {
        now_ms_ += ms;
    }

  private:
    uint32_t now_ms_ = 0;
};

class FakeCommandQueue : public ICommandQueue
{
  public:
    bool enqueue(const RuntimeCommand& command) override
    {
        return queue_.enqueue(command);
    }

    std::size_t cancel(uint32_t dedupe_key) override
    {
        return queue_.cancel(dedupe_key);
    }

    bool popReady(uint32_t now_ms, RuntimeCommand& out) override
    {
        return queue_.popReady(now_ms, out);
    }

    std::size_t size() const
    {
        return queue_.size();
    }

  private:
    FixedCommandQueue<32> queue_{};
};

class FakeEventBus : public IEventSink,
                     public IPersistenceEventSink,
                     public IFeedbackEventSink
{
  public:
    bool publish(const RuntimeEvent& event) override
    {
        return runtime_events_.publish(event);
    }

    bool publish(const PersistenceEvent& event) override
    {
        if (persistence_count_ >= kMaxEvents)
        {
            return false;
        }
        persistence_events_[persistence_count_++] = event;
        return true;
    }

    bool publish(const FeedbackEvent& event) override
    {
        if (feedback_count_ >= kMaxEvents)
        {
            return false;
        }
        feedback_events_[feedback_count_++] = event;
        return true;
    }

    std::size_t drain()
    {
        std::size_t count = 0;
        RuntimeEvent event{};
        while (runtime_events_.pop(event))
        {
            ++count;
        }
        count += persistence_count_;
        count += feedback_count_;
        persistence_count_ = 0;
        feedback_count_ = 0;
        return count;
    }

    std::size_t persistenceCount() const
    {
        return persistence_count_;
    }

    std::size_t feedbackCount() const
    {
        return feedback_count_;
    }

    const PersistenceEvent& persistenceEvent(std::size_t index) const
    {
        return persistence_events_[index];
    }

    const FeedbackEvent& feedbackEvent(std::size_t index) const
    {
        return feedback_events_[index];
    }

  private:
    static constexpr std::size_t kMaxEvents = 32;

    FixedEventSink<32> runtime_events_{};
    PersistenceEvent persistence_events_[kMaxEvents]{};
    FeedbackEvent feedback_events_[kMaxEvents]{};
    std::size_t persistence_count_ = 0;
    std::size_t feedback_count_ = 0;
};

class FakeStorageBackend : public IPlatformStorageAdapter,
                           public IStoreSnapshotProvider,
                           public IStoreStorageAdapter
{
  public:
    void scriptDelay(const char* operation, uint32_t ms)
    {
        (void)operation;
        delay_ms_ = ms;
    }

    void scriptFailure(const char* operation, int32_t error)
    {
        (void)operation;
        fail_ = true;
        error_ = error;
    }

    PlatformStorageResult read(const PlatformStorageReadRequest& request) override
    {
        last_command_id_ = request.command_id;
        return platformResult(request.capacity);
    }

    PlatformStorageResult write(const PlatformStorageWriteRequest& request) override
    {
        last_command_id_ = request.command_id;
        return platformResult(request.len);
    }

    PlatformStorageResult list(const PlatformStorageListRequest& request) override
    {
        last_command_id_ = request.command_id;
        return platformResult(0);
    }

    PlatformStorageResult flush(const PlatformStorageFlushRequest& request) override
    {
        last_command_id_ = request.command_id;
        return platformResult(0);
    }

    StoreSnapshot snapshot(const char* store_key) override
    {
        (void)store_key;
        StoreSnapshot snapshot{};
        snapshot.data = snapshot_;
        snapshot.len = snapshot_len_;
        snapshot.valid = !fail_;
        return snapshot;
    }

    StoreStorageResult write(const char* store_key,
                             const uint8_t* bytes,
                             std::size_t len) override
    {
        (void)store_key;
        (void)bytes;
        StoreStorageResult result{};
        result.ok = !fail_;
        result.bytes = result.ok ? len : 0;
        result.error = result.ok ? 0 : error_;
        ++write_count_;
        return result;
    }

    StoreStorageResult read(const char* store_key,
                            uint8_t* bytes,
                            std::size_t capacity,
                            std::size_t& out_len) override
    {
        (void)store_key;
        (void)bytes;
        out_len = fail_ ? 0 : capacity;
        StoreStorageResult result{};
        result.ok = !fail_;
        result.bytes = out_len;
        result.error = result.ok ? 0 : error_;
        return result;
    }

    uint32_t delayMs() const
    {
        return delay_ms_;
    }

    uint32_t lastCommandId() const
    {
        return last_command_id_;
    }

    std::size_t writeCount() const
    {
        return write_count_;
    }

  private:
    PlatformStorageResult platformResult(std::size_t bytes)
    {
        PlatformStorageResult result{};
        result.ok = !fail_;
        result.bytes = result.ok ? bytes : 0;
        result.error = result.ok ? 0 : error_;
        return result;
    }

    uint8_t snapshot_[4] = {1, 2, 3, 4};
    std::size_t snapshot_len_ = sizeof(snapshot_);
    uint32_t delay_ms_ = 0;
    uint32_t last_command_id_ = 0;
    std::size_t write_count_ = 0;
    bool fail_ = false;
    int32_t error_ = -1;
};

class FakeBusArbiter : public IBusArbiter
{
  public:
    void scriptAcquire(BusAcquireStatus status)
    {
        scripted_status_ = status;
    }

    BusAcquireResult acquire(const BusAcquireRequest& request) override
    {
        ++acquire_count_;
        last_request_ = request;
        BusAcquireResult result{};
        result.status = scripted_status_;
        result.token.valid = scripted_status_ == BusAcquireStatus::Acquired;
        result.token.resource = request.resource;
        result.token.owner = request.command_id;
        result.token.acquired_ms = now_ms_;
        result.diagnostics.resource = request.resource;
        result.diagnostics.command_id = request.command_id;
        result.diagnostics.policy = request.policy;
        if (scripted_status_ != BusAcquireStatus::Acquired)
        {
            health_.status = StorageHealthStatus::Slow;
            health_.last_error = -1;
            health_.last_transition_ms = now_ms_;
        }
        return result;
    }

    void release(const BusAccessToken& token) override
    {
        if (token.valid)
        {
            ++release_count_;
        }
    }

    StorageHealthState health() const override
    {
        return health_;
    }

    BusAcquireRequest diagnostics() const
    {
        return last_request_;
    }

    void setNow(uint32_t now_ms)
    {
        now_ms_ = now_ms;
    }

    std::size_t acquireCount() const
    {
        return acquire_count_;
    }

    std::size_t releaseCount() const
    {
        return release_count_;
    }

  private:
    BusAcquireStatus scripted_status_ = BusAcquireStatus::Acquired;
    BusAcquireRequest last_request_{};
    StorageHealthState health_{};
    uint32_t now_ms_ = 0;
    std::size_t acquire_count_ = 0;
    std::size_t release_count_ = 0;
};

class FakeUiOwner : public IUiEffectSink
{
  public:
    void assertNoBlockingCalls() const
    {
        assert(blocking_calls_ == 0);
    }

    bool apply(const RuntimeUiEffect& effect) override
    {
        last_effect_ = effect;
        ++effect_count_;
        return true;
    }

    void tick()
    {
        ++tick_count_;
    }

    void recordBlockingCall()
    {
        ++blocking_calls_;
    }

    std::size_t effectCount() const
    {
        return effect_count_;
    }

  private:
    RuntimeUiEffect last_effect_{};
    std::size_t effect_count_ = 0;
    std::size_t tick_count_ = 0;
    std::size_t blocking_calls_ = 0;
};

class FakeFeedbackPresenter : public IFeedbackPresenter
{
  public:
    bool present(const NoticeIntent& intent) override
    {
        if (count_ >= kMaxNotices)
        {
            return false;
        }
        captured_[count_++] = intent;
        return true;
    }

    std::size_t captured() const
    {
        return count_;
    }

    const NoticeIntent& capturedNotice(std::size_t index) const
    {
        return captured_[index];
    }

  private:
    static constexpr std::size_t kMaxNotices = 16;
    NoticeIntent captured_[kMaxNotices]{};
    std::size_t count_ = 0;
};

class RuntimeHarness
{
  public:
    FakeClock& clock()
    {
        return clock_;
    }

    FakeCommandQueue& commands()
    {
        return commands_;
    }

    FakeEventBus& events()
    {
        return events_;
    }

    FakeStorageBackend& storage()
    {
        return storage_;
    }

    FakeBusArbiter& bus()
    {
        return bus_;
    }

    FakeUiOwner& ui()
    {
        return ui_;
    }

    FakeFeedbackPresenter& feedbackPresenter()
    {
        return feedback_;
    }

    void advance(uint32_t ms)
    {
        clock_.advance(ms);
        bus_.setNow(clock_.now());
    }

    std::size_t drainUi()
    {
        ui_.tick();
        return events_.drain();
    }

    void runUntilIdle(uint32_t step_ms = 10, uint32_t max_steps = 64)
    {
        for (uint32_t i = 0; i < max_steps; ++i)
        {
            advance(step_ms);
            if (drainUi() == 0)
            {
                return;
            }
        }
    }

  private:
    FakeClock clock_{};
    FakeCommandQueue commands_{};
    FakeEventBus events_{};
    FakeStorageBackend storage_{};
    FakeBusArbiter bus_{};
    FakeUiOwner ui_{};
    FakeFeedbackPresenter feedback_{};
};

} // namespace runtime
} // namespace sys
