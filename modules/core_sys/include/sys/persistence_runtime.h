#pragma once

#include "sys/runtime_async.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sys
{
namespace runtime
{

enum class PersistencePolicyMode : uint8_t
{
    DebouncedSave,
    BatchSave,
    ImmediateCriticalSave,
    DropDuplicateSave,
};

enum class PersistenceEventKind : uint8_t
{
    SaveQueued,
    SaveStarted,
    SaveSucceeded,
    SaveFailed,
    SaveCoalesced,
    SaveDropped,
};

struct StoreSnapshot
{
    const uint8_t* data = nullptr;
    std::size_t len = 0;
    bool valid = false;
};

struct StoreStorageResult
{
    bool ok = false;
    std::size_t bytes = 0;
    int32_t error = 0;
};

struct PersistenceCommand
{
    uint32_t command_id = 0;
    const char* store_key = nullptr;
    PersistencePolicyMode policy = PersistencePolicyMode::DebouncedSave;
    uint32_t deadline_ms = 0;
    uint32_t created_at_ms = 0;
};

struct PersistenceEvent
{
    PersistenceEventKind kind = PersistenceEventKind::SaveFailed;
    const char* store_key = nullptr;
    uint32_t command_id = 0;
    uint32_t timestamp_ms = 0;
    int32_t error = 0;
};

class PersistencePolicy
{
  public:
    virtual ~PersistencePolicy() = default;

    virtual PersistencePolicyMode modeFor(const char* store_key) const = 0;
    virtual uint32_t delayFor(PersistencePolicyMode policy) const = 0;
    virtual BusAccessPolicy busPolicyFor(PersistencePolicyMode policy) const = 0;
};

class DefaultPersistencePolicy : public PersistencePolicy
{
  public:
    PersistencePolicyMode modeFor(const char* store_key) const override
    {
        (void)store_key;
        return PersistencePolicyMode::DebouncedSave;
    }

    uint32_t delayFor(PersistencePolicyMode policy) const override
    {
        switch (policy)
        {
        case PersistencePolicyMode::ImmediateCriticalSave:
            return 0;
        case PersistencePolicyMode::BatchSave:
            return 500;
        case PersistencePolicyMode::DropDuplicateSave:
        case PersistencePolicyMode::DebouncedSave:
        default:
            return 150;
        }
    }

    BusAccessPolicy busPolicyFor(PersistencePolicyMode policy) const override
    {
        return policy == PersistencePolicyMode::ImmediateCriticalSave
                   ? BusAccessPolicy::DurableCommit
                   : BusAccessPolicy::BackgroundWorkerBounded;
    }
};

class IStoreSnapshotProvider
{
  public:
    virtual ~IStoreSnapshotProvider() = default;

    virtual StoreSnapshot snapshot(const char* store_key) = 0;
};

class IStoreStorageAdapter
{
  public:
    virtual ~IStoreStorageAdapter() = default;

    virtual StoreStorageResult write(const char* store_key,
                                     const uint8_t* bytes,
                                     std::size_t len) = 0;
    virtual StoreStorageResult read(const char* store_key,
                                    uint8_t* bytes,
                                    std::size_t capacity,
                                    std::size_t& out_len) = 0;
};

class IPersistenceEventSink
{
  public:
    virtual ~IPersistenceEventSink() = default;

    virtual bool publish(const PersistenceEvent& event) = 0;
};

template <std::size_t N>
class DirtyStoreRegistry
{
  public:
    bool markDirty(const char* store_key,
                   PersistencePolicyMode policy,
                   uint32_t due_ms)
    {
        if (store_key == nullptr || store_key[0] == '\0')
        {
            return false;
        }

        for (std::size_t i = 0; i < count_; ++i)
        {
            if (sameKey(records_[i].store_key, store_key))
            {
                records_[i].policy = policy;
                records_[i].due_ms = due_ms;
                records_[i].dirty = true;
                return true;
            }
        }

        if (count_ >= N)
        {
            return false;
        }

        records_[count_++] = DirtyStoreRecord{store_key, due_ms, policy, true};
        return true;
    }

    bool takeDue(uint32_t now_ms, PersistenceCommand& out)
    {
        for (std::size_t i = 0; i < count_; ++i)
        {
            DirtyStoreRecord& record = records_[i];
            if (!record.dirty || static_cast<int32_t>(record.due_ms - now_ms) > 0)
            {
                continue;
            }
            out.store_key = record.store_key;
            out.policy = record.policy;
            out.created_at_ms = now_ms;
            record.dirty = false;
            compact();
            return true;
        }
        return false;
    }

    bool hasPending(const char* store_key) const
    {
        for (std::size_t i = 0; i < count_; ++i)
        {
            if (records_[i].dirty && sameKey(records_[i].store_key, store_key))
            {
                return true;
            }
        }
        return false;
    }

    std::size_t size() const
    {
        return count_;
    }

  private:
    struct DirtyStoreRecord
    {
        const char* store_key = nullptr;
        uint32_t due_ms = 0;
        PersistencePolicyMode policy = PersistencePolicyMode::DebouncedSave;
        bool dirty = false;
    };

    static bool sameKey(const char* lhs, const char* rhs)
    {
        return lhs != nullptr && rhs != nullptr && std::strcmp(lhs, rhs) == 0;
    }

    void compact()
    {
        DirtyStoreRecord kept[N]{};
        std::size_t kept_count = 0;
        for (std::size_t i = 0; i < count_; ++i)
        {
            if (records_[i].dirty)
            {
                kept[kept_count++] = records_[i];
            }
        }
        for (std::size_t i = 0; i < kept_count; ++i)
        {
            records_[i] = kept[i];
        }
        count_ = kept_count;
    }

    DirtyStoreRecord records_[N]{};
    std::size_t count_ = 0;
};

class PersistenceWorker
{
  public:
    PersistenceWorker(IStoreSnapshotProvider& snapshots,
                      IStoreStorageAdapter& storage,
                      IBusArbiter& bus,
                      IPersistenceEventSink& events,
                      PersistencePolicy& policy)
        : snapshots_(snapshots), storage_(storage), bus_(bus), events_(events), policy_(policy)
    {
    }

    bool submit(const PersistenceCommand& command)
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

        PersistenceEvent started{};
        started.kind = PersistenceEventKind::SaveStarted;
        started.store_key = pending_command_.store_key;
        started.command_id = pending_command_.command_id;
        started.timestamp_ms = now_ms;
        (void)events_.publish(started);

        BusAcquireRequest request{};
        request.policy = policy_.busPolicyFor(pending_command_.policy);
        request.command_id = pending_command_.command_id;
        request.deadline_ms = pending_command_.deadline_ms;
        const BusAcquireResult acquire = bus_.acquire(request);
        if (acquire.status != BusAcquireStatus::Acquired)
        {
            publishResult(PersistenceEventKind::SaveFailed, now_ms, -11);
            pending_ = false;
            return;
        }

        StoreSnapshot snapshot = snapshots_.snapshot(pending_command_.store_key);
        StoreStorageResult result{};
        if (snapshot.valid)
        {
            result = storage_.write(pending_command_.store_key, snapshot.data, snapshot.len);
        }
        else
        {
            result.error = -12;
        }
        bus_.release(acquire.token);

        publishResult(result.ok ? PersistenceEventKind::SaveSucceeded
                                : PersistenceEventKind::SaveFailed,
                      now_ms,
                      result.ok ? 0 : result.error);
        pending_ = false;
    }

    bool busy() const
    {
        return pending_;
    }

  private:
    void publishResult(PersistenceEventKind kind, uint32_t now_ms, int32_t error)
    {
        PersistenceEvent event{};
        event.kind = kind;
        event.store_key = pending_command_.store_key;
        event.command_id = pending_command_.command_id;
        event.timestamp_ms = now_ms;
        event.error = error;
        (void)events_.publish(event);
    }

    IStoreSnapshotProvider& snapshots_;
    IStoreStorageAdapter& storage_;
    IBusArbiter& bus_;
    IPersistenceEventSink& events_;
    PersistencePolicy& policy_;
    PersistenceCommand pending_command_{};
    bool pending_ = false;
};

template <std::size_t N>
class PersistenceRuntime
{
  public:
    PersistenceRuntime(DirtyStoreRegistry<N>& registry,
                       PersistenceWorker& worker,
                       IPersistenceEventSink& events,
                       PersistencePolicy& policy)
        : registry_(registry), worker_(worker), events_(events), policy_(policy)
    {
    }

    bool markDirty(const char* store_key, uint32_t now_ms)
    {
        const PersistencePolicyMode mode = policy_.modeFor(store_key);
        const bool marked = registry_.markDirty(store_key, mode, now_ms + policy_.delayFor(mode));
        PersistenceEvent event{};
        event.kind = marked ? PersistenceEventKind::SaveQueued
                            : PersistenceEventKind::SaveDropped;
        event.store_key = store_key;
        event.timestamp_ms = now_ms;
        (void)events_.publish(event);
        return marked;
    }

    bool requestSave(const char* store_key,
                     PersistencePolicyMode policy,
                     uint32_t now_ms)
    {
        return registry_.markDirty(store_key, policy, now_ms + policy_.delayFor(policy));
    }

    void tick(uint32_t now_ms)
    {
        if (!worker_.busy())
        {
            PersistenceCommand command{};
            if (registry_.takeDue(now_ms, command))
            {
                command.command_id = next_command_id_++;
                if (!worker_.submit(command))
                {
                    (void)registry_.markDirty(command.store_key,
                                              command.policy,
                                              now_ms + policy_.delayFor(command.policy));
                }
            }
        }
        worker_.tick(now_ms);
    }

    void handle(const PersistenceEvent& event)
    {
        last_event_ = event;
    }

    PersistenceEvent lastEvent() const
    {
        return last_event_;
    }

  private:
    DirtyStoreRegistry<N>& registry_;
    PersistenceWorker& worker_;
    IPersistenceEventSink& events_;
    PersistencePolicy& policy_;
    PersistenceEvent last_event_{};
    uint32_t next_command_id_ = 1;
};

} // namespace runtime
} // namespace sys
