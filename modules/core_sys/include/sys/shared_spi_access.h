#pragma once

#include <cstdint>

namespace sys::runtime
{

// Technical shared-bus primitives. Business/runtime modules must depend on
// semantic device services instead of including this header.
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

struct BusAcquireRequest
{
    uint32_t resource = 0;
    BusAccessPolicy policy = BusAccessPolicy::BackgroundWorkerBounded;
    uint32_t command_id = 0;
    uint32_t deadline_ms = 0;
    uint32_t origin = 0;
    const char* owner_label = nullptr;
};

struct BusAccessToken
{
    uint32_t resource = 0;
    uint32_t owner = 0;
    uint32_t acquired_ms = 0;
    uint32_t generation = 0;
    uint32_t depth = 0;
    uintptr_t task_id = 0;
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

class IBusArbiter
{
  public:
    virtual ~IBusArbiter() = default;

    virtual BusAcquireResult acquire(const BusAcquireRequest& request) = 0;
    virtual void release(const BusAccessToken& token) = 0;
    virtual StorageHealthState health() const = 0;
};

} // namespace sys::runtime
