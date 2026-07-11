#pragma once

#include "sys/bus_access_scope.h"
#include "sys/clock.h"
#include "sys/runtime_async.h"

#include <cstdint>

namespace platform::esp::arduino_common::storage
{

class PersistenceBusGate final
{
  public:
    PersistenceBusGate(sys::runtime::IBusArbiter& arbiter,
                       sys::runtime::BusAccessPolicy policy,
                       uint32_t wait_ms,
                       uint32_t resource,
                       uint32_t command_id,
                       uint32_t origin)
        : scope_(arbiter, makeRequest(policy, wait_ms, resource, command_id, origin))
    {
    }

    bool locked() const
    {
        return scope_.acquired();
    }

    sys::runtime::BusAcquireStatus status() const
    {
        return scope_.status();
    }

  private:
    static sys::runtime::BusAcquireRequest makeRequest(
        sys::runtime::BusAccessPolicy policy,
        uint32_t wait_ms,
        uint32_t resource,
        uint32_t command_id,
        uint32_t origin)
    {
        sys::runtime::BusAcquireRequest request{};
        request.resource = resource;
        request.policy = policy;
        request.command_id = command_id;
        request.origin = origin;
        request.deadline_ms = sys::millis_now() + wait_ms;
        return request;
    }

    sys::runtime::ScopedBusAccessToken scope_;
};

} // namespace platform::esp::arduino_common::storage
