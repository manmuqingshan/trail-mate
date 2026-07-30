#pragma once

#include "sys/feedback_runtime.h"
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
                     public IFeedbackEventSink
{
  public:
    bool publish(const RuntimeEvent& event) override
    {
        return runtime_events_.publish(event);
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
        count += feedback_count_;
        feedback_count_ = 0;
        return count;
    }

    std::size_t feedbackCount() const
    {
        return feedback_count_;
    }

    const FeedbackEvent& feedbackEvent(std::size_t index) const
    {
        return feedback_events_[index];
    }

  private:
    static constexpr std::size_t kMaxEvents = 32;

    FixedEventSink<32> runtime_events_{};
    FeedbackEvent feedback_events_[kMaxEvents]{};
    std::size_t feedback_count_ = 0;
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
    FakeBusArbiter bus_{};
    FakeUiOwner ui_{};
    FakeFeedbackPresenter feedback_{};
};

} // namespace runtime
} // namespace sys
