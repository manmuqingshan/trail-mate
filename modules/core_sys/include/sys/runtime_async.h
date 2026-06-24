#pragma once

#include <cstddef>
#include <cstdint>

namespace sys
{
namespace runtime
{

enum class RuntimeCommandKind : uint8_t
{
    Unknown,
    MapTileLoad,
    MapTileCancelGeneration,
    TrackStart,
    TrackStop,
    TrackAppendPoint,
    TrackFlush,
    TrackList,
    PersistenceSave,
    FeedbackNotice,
    ProtocolAction,
};

enum class RuntimeEventKind : uint8_t
{
    Unknown,
    CommandQueued,
    CommandStarted,
    CommandCompleted,
    CommandFailed,
    CommandCancelled,
    ResourceBusy,
    StorageSlow,
    StorageDegraded,
    MapTileReady,
    MapTileFailed,
    TrackStarted,
    TrackStopped,
    TrackFlushSucceeded,
    TrackFlushFailed,
    PersistenceSaved,
    PersistenceFailed,
    FeedbackPosted,
    FeedbackDropped,
};

enum class RuntimeStatus : uint8_t
{
    Idle,
    Queued,
    Running,
    Degraded,
    Failed,
};

enum class RuntimePriority : uint8_t
{
    Realtime = 0,
    Interactive = 16,
    Normal = 64,
    Background = 128,
    Idle = 192,
};

enum class RuntimeCancelPolicy : uint8_t
{
    None,
    CancelByGeneration,
    CancelByDedupeKey,
    DropIfStale,
};

struct RuntimeIntent
{
    RuntimeCommandKind kind = RuntimeCommandKind::Unknown;
    uint32_t origin = 0;
    uint32_t generation = 0;
    uint32_t dedupe_key = 0;
    uint32_t deadline_ms = 0;
    uint32_t submitted_at_ms = 0;
    RuntimeCancelPolicy cancel_policy = RuntimeCancelPolicy::None;
    RuntimePriority priority_hint = RuntimePriority::Normal;
};

enum class BusAccessPolicy : uint8_t
{
    UiNeverBlock,
    DisplayFrameCritical,
    InteractiveWorkerBounded,
    BackgroundWorkerBounded,
    DurableCommit,
    RecoveryExclusive,
};

enum class BusAcquireStatus : uint8_t
{
    Acquired,
    Busy,
    TimedOut,
    Unavailable,
};

enum class StorageHealthStatus : uint8_t
{
    Healthy,
    Slow,
    Degraded,
    Unavailable,
    Recovering,
};

struct RuntimeCommand
{
    uint32_t command_id = 0;
    RuntimeCommandKind kind = RuntimeCommandKind::Unknown;
    RuntimePriority priority = RuntimePriority::Normal;
    RuntimeCancelPolicy cancel_policy = RuntimeCancelPolicy::None;
    uint32_t created_at_ms = 0;
    uint32_t deadline_ms = 0;
    uint32_t generation = 0;
    uint32_t dedupe_key = 0;
    uint32_t origin = 0;
};

struct RuntimeEvent
{
    uint32_t event_id = 0;
    RuntimeEventKind kind = RuntimeEventKind::Unknown;
    uint32_t command_id = 0;
    uint32_t timestamp_ms = 0;
    uint32_t generation = 0;
    int32_t error = 0;
};

struct RuntimeState
{
    RuntimeStatus status = RuntimeStatus::Idle;
    RuntimeCommandKind active_kind = RuntimeCommandKind::Unknown;
    uint32_t active_command_id = 0;
    uint32_t last_event_id = 0;
    int32_t last_error = 0;
};

struct BusAcquireRequest
{
    uint32_t resource = 0;
    BusAccessPolicy policy = BusAccessPolicy::BackgroundWorkerBounded;
    uint32_t command_id = 0;
    uint32_t deadline_ms = 0;
    uint32_t origin = 0;
};

struct BusAccessToken
{
    uint32_t resource = 0;
    uint32_t owner = 0;
    uint32_t acquired_ms = 0;
    bool valid = false;
};

struct BusDiagnostics
{
    uint32_t resource = 0;
    uint32_t owner = 0;
    uint32_t command_id = 0;
    uint32_t wait_ms = 0;
    uint32_t hold_ms = 0;
    BusAccessPolicy policy = BusAccessPolicy::BackgroundWorkerBounded;
};

struct BusAcquireResult
{
    BusAcquireStatus status = BusAcquireStatus::Unavailable;
    BusAccessToken token{};
    BusDiagnostics diagnostics{};
};

struct StorageHealthState
{
    StorageHealthStatus status = StorageHealthStatus::Healthy;
    int32_t last_error = 0;
    uint32_t last_transition_ms = 0;
};

struct RuntimeRetryDecision
{
    bool retry = false;
    uint32_t delay_ms = 0;
};

struct PlatformStorageReadRequest
{
    uint32_t command_id = 0;
    const char* path = nullptr;
    uint8_t* buffer = nullptr;
    std::size_t capacity = 0;
};

struct PlatformStorageWriteRequest
{
    uint32_t command_id = 0;
    const char* path = nullptr;
    const uint8_t* data = nullptr;
    std::size_t len = 0;
    bool durable = false;
};

struct PlatformStorageListRequest
{
    uint32_t command_id = 0;
    const char* path = nullptr;
};

struct PlatformStorageFlushRequest
{
    uint32_t command_id = 0;
    uint32_t handle = 0;
};

struct PlatformStorageResult
{
    bool ok = false;
    std::size_t bytes = 0;
    int32_t error = 0;
};

struct RuntimeUiEffect
{
    RuntimeEventKind kind = RuntimeEventKind::Unknown;
    uint32_t event_id = 0;
    uint32_t command_id = 0;
    int32_t error = 0;
};

class ICommandQueue
{
  public:
    virtual ~ICommandQueue() = default;

    virtual bool enqueue(const RuntimeCommand& command) = 0;
    virtual std::size_t cancel(uint32_t dedupe_key) = 0;
    virtual bool popReady(uint32_t now_ms, RuntimeCommand& out) = 0;
};

class IEventSink
{
  public:
    virtual ~IEventSink() = default;

    virtual bool publish(const RuntimeEvent& event) = 0;
};

class IActiveWorker
{
  public:
    virtual ~IActiveWorker() = default;

    virtual void tick(uint32_t now_ms) = 0;
    virtual bool submit(const RuntimeCommand& command) = 0;
};

class IBusArbiter
{
  public:
    virtual ~IBusArbiter() = default;

    virtual BusAcquireResult acquire(const BusAcquireRequest& request) = 0;
    virtual void release(const BusAccessToken& token) = 0;
    virtual StorageHealthState health() const = 0;
};

class IBusAdapter
{
  public:
    virtual ~IBusAdapter() = default;

    virtual bool tryAcquire(uint32_t timeout_ms) = 0;
    virtual void release() = 0;
    virtual uint32_t nowMs() const = 0;
    virtual uint32_t owner() const = 0;
};

class BusPolicyStrategy
{
  public:
    virtual ~BusPolicyStrategy() = default;

    virtual BusAccessPolicy select(const RuntimeCommand& command) const = 0;
    virtual uint32_t timeoutFor(BusAccessPolicy policy) const = 0;
};

class DefaultBusPolicyStrategy : public BusPolicyStrategy
{
  public:
    BusAccessPolicy select(const RuntimeCommand& command) const override
    {
        if (command.kind == RuntimeCommandKind::MapTileLoad)
        {
            return BusAccessPolicy::DisplayFrameCritical;
        }
        if (command.priority == RuntimePriority::Realtime ||
            command.priority == RuntimePriority::Interactive)
        {
            return BusAccessPolicy::InteractiveWorkerBounded;
        }
        if (command.kind == RuntimeCommandKind::TrackStop ||
            command.kind == RuntimeCommandKind::TrackFlush)
        {
            return BusAccessPolicy::DurableCommit;
        }
        return BusAccessPolicy::BackgroundWorkerBounded;
    }

    uint32_t timeoutFor(BusAccessPolicy policy) const override
    {
        switch (policy)
        {
        case BusAccessPolicy::UiNeverBlock:
        case BusAccessPolicy::DisplayFrameCritical:
            return 0;
        case BusAccessPolicy::InteractiveWorkerBounded:
            return 2;
        case BusAccessPolicy::BackgroundWorkerBounded:
            return 25;
        case BusAccessPolicy::DurableCommit:
            return 150;
        case BusAccessPolicy::RecoveryExclusive:
            return 500;
        default:
            return 0;
        }
    }
};

class StorageBusArbiter : public IBusArbiter
{
  public:
    StorageBusArbiter(IBusAdapter& adapter, BusPolicyStrategy& policy)
        : adapter_(adapter), policy_(policy)
    {
    }

    BusAcquireResult acquire(const BusAcquireRequest& request) override
    {
        const uint32_t start_ms = adapter_.nowMs();
        uint32_t timeout_ms = policy_.timeoutFor(request.policy);
        if (request.deadline_ms != 0)
        {
            const uint32_t remaining =
                static_cast<int32_t>(request.deadline_ms - start_ms) > 0
                    ? request.deadline_ms - start_ms
                    : 0;
            if (remaining < timeout_ms)
            {
                timeout_ms = remaining;
            }
        }

        const bool acquired = adapter_.tryAcquire(timeout_ms);
        const uint32_t end_ms = adapter_.nowMs();

        BusAcquireResult result{};
        result.status = acquired ? BusAcquireStatus::Acquired
                                 : (timeout_ms == 0 ? BusAcquireStatus::Busy
                                                    : BusAcquireStatus::TimedOut);
        result.token.resource = request.resource;
        result.token.owner = request.command_id;
        result.token.acquired_ms = acquired ? end_ms : 0;
        result.token.valid = acquired;
        result.diagnostics.resource = request.resource;
        result.diagnostics.owner = adapter_.owner();
        result.diagnostics.command_id = request.command_id;
        result.diagnostics.wait_ms = end_ms - start_ms;
        result.diagnostics.policy = request.policy;

        updateHealth(result.status, end_ms);
        return result;
    }

    void release(const BusAccessToken& token) override
    {
        if (!token.valid)
        {
            return;
        }
        adapter_.release();
        consecutive_timeouts_ = 0;
        if (health_.status == StorageHealthStatus::Slow ||
            health_.status == StorageHealthStatus::Recovering)
        {
            health_.status = StorageHealthStatus::Healthy;
            health_.last_error = 0;
            health_.last_transition_ms = adapter_.nowMs();
        }
    }

    StorageHealthState health() const override
    {
        return health_;
    }

    BusAccessPolicy selectPolicy(const RuntimeCommand& command) const
    {
        return policy_.select(command);
    }

  private:
    void updateHealth(BusAcquireStatus status, uint32_t now_ms)
    {
        if (status == BusAcquireStatus::Acquired)
        {
            return;
        }

        health_.last_transition_ms = now_ms;
        if (status == BusAcquireStatus::Unavailable)
        {
            health_.status = StorageHealthStatus::Unavailable;
            health_.last_error = -3;
            return;
        }

        ++consecutive_timeouts_;
        health_.last_error = status == BusAcquireStatus::TimedOut ? -2 : -1;
        health_.status = consecutive_timeouts_ >= 3 ? StorageHealthStatus::Degraded
                                                    : StorageHealthStatus::Slow;
    }

    IBusAdapter& adapter_;
    BusPolicyStrategy& policy_;
    StorageHealthState health_{};
    uint8_t consecutive_timeouts_ = 0;
};

class IPlatformStorageAdapter
{
  public:
    virtual ~IPlatformStorageAdapter() = default;

    virtual PlatformStorageResult read(const PlatformStorageReadRequest& request) = 0;
    virtual PlatformStorageResult write(const PlatformStorageWriteRequest& request) = 0;
    virtual PlatformStorageResult list(const PlatformStorageListRequest& request) = 0;
    virtual PlatformStorageResult flush(const PlatformStorageFlushRequest& request) = 0;
};

class IUiEffectSink
{
  public:
    virtual ~IUiEffectSink() = default;

    virtual bool apply(const RuntimeUiEffect& effect) = 0;
};

class IUiOwnerGuard
{
  public:
    virtual ~IUiOwnerGuard() = default;

    virtual bool isUiOwner() const = 0;
    virtual void recordForbiddenBlockingCall(RuntimeCommandKind kind) = 0;
};

class RuntimePolicyStrategy
{
  public:
    virtual ~RuntimePolicyStrategy() = default;

    virtual RuntimePriority selectPriority(const RuntimeIntent& intent) const = 0;
    virtual BusAccessPolicy selectBusPolicy(const RuntimeCommand& command) const = 0;
    virtual RuntimeRetryDecision selectRetry(const RuntimeCommand& command,
                                             const PlatformStorageResult& result) const = 0;
};

class DefaultRuntimePolicyStrategy : public RuntimePolicyStrategy
{
  public:
    RuntimePriority selectPriority(const RuntimeIntent& intent) const override
    {
        return intent.priority_hint;
    }

    BusAccessPolicy selectBusPolicy(const RuntimeCommand& command) const override
    {
        if (command.kind == RuntimeCommandKind::MapTileLoad)
        {
            return BusAccessPolicy::DisplayFrameCritical;
        }
        if (command.priority == RuntimePriority::Realtime ||
            command.priority == RuntimePriority::Interactive)
        {
            return BusAccessPolicy::InteractiveWorkerBounded;
        }
        if (command.priority == RuntimePriority::Idle ||
            command.priority == RuntimePriority::Background)
        {
            return BusAccessPolicy::BackgroundWorkerBounded;
        }
        return BusAccessPolicy::BackgroundWorkerBounded;
    }

    RuntimeRetryDecision selectRetry(const RuntimeCommand& command,
                                     const PlatformStorageResult& result) const override
    {
        RuntimeRetryDecision decision{};
        if (!result.ok && command.cancel_policy != RuntimeCancelPolicy::DropIfStale)
        {
            decision.retry = true;
            decision.delay_ms = 250;
        }
        return decision;
    }
};

class RuntimeFacade
{
  public:
    RuntimeFacade(ICommandQueue& commands,
                  IActiveWorker& worker,
                  IEventSink& events,
                  RuntimeState& state,
                  RuntimePolicyStrategy& policy)
        : commands_(commands), worker_(worker), events_(events), state_(state), policy_(policy)
    {
    }

    bool submit(const RuntimeIntent& intent)
    {
        RuntimeCommand command{};
        command.command_id = next_command_id_++;
        command.kind = intent.kind;
        command.priority = policy_.selectPriority(intent);
        command.cancel_policy = intent.cancel_policy;
        command.created_at_ms = intent.submitted_at_ms;
        command.deadline_ms = intent.deadline_ms;
        command.generation = intent.generation;
        command.dedupe_key = intent.dedupe_key;
        command.origin = intent.origin;

        if (command.cancel_policy == RuntimeCancelPolicy::CancelByDedupeKey &&
            command.dedupe_key != 0)
        {
            (void)commands_.cancel(command.dedupe_key);
        }

        const bool queued = commands_.enqueue(command);
        RuntimeEvent event{};
        event.event_id = next_event_id_++;
        event.kind = queued ? RuntimeEventKind::CommandQueued : RuntimeEventKind::CommandFailed;
        event.command_id = command.command_id;
        event.timestamp_ms = intent.submitted_at_ms;
        event.generation = command.generation;
        event.error = queued ? 0 : -1;
        (void)publishEvent(event);
        state_.status = queued ? RuntimeStatus::Queued : RuntimeStatus::Failed;
        state_.active_kind = intent.kind;
        state_.active_command_id = command.command_id;
        state_.last_event_id = event.event_id;
        state_.last_error = event.error;
        return queued;
    }

    void tick(uint32_t now_ms)
    {
        RuntimeCommand command{};
        while (commands_.popReady(now_ms, command))
        {
            state_.status = RuntimeStatus::Running;
            state_.active_kind = command.kind;
            state_.active_command_id = command.command_id;

            RuntimeEvent started{};
            started.event_id = next_event_id_++;
            started.kind = RuntimeEventKind::CommandStarted;
            started.command_id = command.command_id;
            started.timestamp_ms = now_ms;
            started.generation = command.generation;
            (void)publishEvent(started);
            state_.last_event_id = started.event_id;

            if (!worker_.submit(command))
            {
                RuntimeEvent failed{};
                failed.event_id = next_event_id_++;
                failed.kind = RuntimeEventKind::CommandFailed;
                failed.command_id = command.command_id;
                failed.timestamp_ms = now_ms;
                failed.generation = command.generation;
                failed.error = -1;
                (void)publishEvent(failed);
                state_.status = RuntimeStatus::Failed;
                state_.last_event_id = failed.event_id;
                state_.last_error = failed.error;
            }
        }
        worker_.tick(now_ms);
    }

    std::size_t drainEvents()
    {
        const std::size_t drained = pending_event_count_;
        pending_event_count_ = 0;
        return drained;
    }

  private:
    bool publishEvent(const RuntimeEvent& event)
    {
        const bool ok = events_.publish(event);
        if (ok)
        {
            ++pending_event_count_;
        }
        return ok;
    }

    ICommandQueue& commands_;
    IActiveWorker& worker_;
    IEventSink& events_;
    RuntimeState& state_;
    RuntimePolicyStrategy& policy_;
    uint32_t next_command_id_ = 1;
    uint32_t next_event_id_ = 1;
    std::size_t pending_event_count_ = 0;
};

class RuntimeEventUiEffectBridge : public IEventSink
{
  public:
    RuntimeEventUiEffectBridge(IEventSink& events, IUiEffectSink& effects)
        : events_(events), effects_(effects)
    {
    }

    bool publish(const RuntimeEvent& event) override
    {
        const bool published = events_.publish(event);
        RuntimeUiEffect effect{};
        effect.kind = event.kind;
        effect.event_id = event.event_id;
        effect.command_id = event.command_id;
        effect.error = event.error;
        (void)effects_.apply(effect);
        return published;
    }

  private:
    IEventSink& events_;
    IUiEffectSink& effects_;
};

template <typename T, std::size_t N>
class FixedRuntimeQueue
{
  public:
    FixedRuntimeQueue()
    {
        static_assert(N > 0, "FixedRuntimeQueue capacity must be > 0");
    }

    bool enqueue(const T& item)
    {
        if (size_ >= N)
        {
            return false;
        }
        items_[(head_ + size_) % N] = item;
        ++size_;
        return true;
    }

    bool pop(T& out)
    {
        if (size_ == 0)
        {
            return false;
        }
        out = items_[head_];
        head_ = (head_ + 1) % N;
        --size_;
        return true;
    }

    std::size_t size() const
    {
        return size_;
    }

    constexpr std::size_t capacity() const
    {
        return N;
    }

    bool empty() const
    {
        return size_ == 0;
    }

    bool full() const
    {
        return size_ >= N;
    }

    void clear()
    {
        head_ = 0;
        size_ = 0;
    }

    const T* at(std::size_t index) const
    {
        if (index >= size_)
        {
            return nullptr;
        }
        return &items_[(head_ + index) % N];
    }

    T* at(std::size_t index)
    {
        if (index >= size_)
        {
            return nullptr;
        }
        return &items_[(head_ + index) % N];
    }

  private:
    T items_[N]{};
    std::size_t head_ = 0;
    std::size_t size_ = 0;
};

template <std::size_t N>
class FixedCommandQueue : public ICommandQueue
{
  public:
    bool enqueue(const RuntimeCommand& command) override
    {
        return queue_.enqueue(command);
    }

    bool enqueueOrReplaceDedupe(const RuntimeCommand& command)
    {
        if (command.dedupe_key != 0)
        {
            for (std::size_t i = 0; i < queue_.size(); ++i)
            {
                RuntimeCommand* existing = queue_.at(i);
                if (existing && existing->dedupe_key == command.dedupe_key)
                {
                    *existing = command;
                    return true;
                }
            }
        }
        return queue_.enqueue(command);
    }

    bool popNext(uint32_t now_ms, RuntimeCommand& out)
    {
        if (queue_.empty())
        {
            return false;
        }

        std::size_t best_index = N;
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            const RuntimeCommand* candidate = queue_.at(i);
            if (!candidate)
            {
                continue;
            }
            if (candidate->deadline_ms != 0 &&
                static_cast<int32_t>(candidate->deadline_ms - now_ms) < 0)
            {
                continue;
            }
            if (best_index == N)
            {
                best_index = i;
                continue;
            }
            const RuntimeCommand* best = queue_.at(best_index);
            if (!best ||
                static_cast<uint8_t>(candidate->priority) < static_cast<uint8_t>(best->priority) ||
                (candidate->priority == best->priority &&
                 candidate->created_at_ms < best->created_at_ms))
            {
                best_index = i;
            }
        }

        if (best_index == N)
        {
            return false;
        }

        RuntimeCommand selected{};
        FixedRuntimeQueue<RuntimeCommand, N> rebuilt;
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            const RuntimeCommand* item = queue_.at(i);
            if (!item)
            {
                continue;
            }
            if (i == best_index)
            {
                selected = *item;
            }
            else
            {
                (void)rebuilt.enqueue(*item);
            }
        }
        queue_ = rebuilt;
        out = selected;
        return true;
    }

    bool popReady(uint32_t now_ms, RuntimeCommand& out) override
    {
        return popNext(now_ms, out);
    }

    std::size_t cancelGeneration(uint32_t generation)
    {
        return removeMatching(generation, 0, true);
    }

    std::size_t cancelDedupeKey(uint32_t dedupe_key)
    {
        return removeMatching(0, dedupe_key, false);
    }

    std::size_t cancel(uint32_t dedupe_key) override
    {
        return cancelDedupeKey(dedupe_key);
    }

    std::size_t size() const
    {
        return queue_.size();
    }

    bool empty() const
    {
        return queue_.empty();
    }

    void clear()
    {
        queue_.clear();
    }

    const RuntimeCommand* at(std::size_t index) const
    {
        return queue_.at(index);
    }

  private:
    std::size_t removeMatching(uint32_t generation, uint32_t dedupe_key, bool by_generation)
    {
        FixedRuntimeQueue<RuntimeCommand, N> rebuilt;
        std::size_t removed = 0;
        for (std::size_t i = 0; i < queue_.size(); ++i)
        {
            const RuntimeCommand* item = queue_.at(i);
            if (!item)
            {
                continue;
            }
            const bool match = by_generation ? item->generation == generation
                                             : item->dedupe_key == dedupe_key;
            if (match)
            {
                ++removed;
                continue;
            }
            (void)rebuilt.enqueue(*item);
        }
        queue_ = rebuilt;
        return removed;
    }

    FixedRuntimeQueue<RuntimeCommand, N> queue_;
};

template <std::size_t N>
using FixedEventQueue = FixedRuntimeQueue<RuntimeEvent, N>;

template <std::size_t N>
class FixedEventSink : public IEventSink
{
  public:
    bool publish(const RuntimeEvent& event) override
    {
        return queue_.enqueue(event);
    }

    bool pop(RuntimeEvent& out)
    {
        return queue_.pop(out);
    }

    std::size_t size() const
    {
        return queue_.size();
    }

  private:
    FixedEventQueue<N> queue_{};
};

} // namespace runtime
} // namespace sys
