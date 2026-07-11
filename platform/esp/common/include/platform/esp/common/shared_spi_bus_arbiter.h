#pragma once

#include "platform/esp/common/shared_spi_lock.h"
#include "sys/clock.h"
#include "sys/runtime_async.h"

#include <cstdint>

namespace platform::esp::common
{

class SharedSpiBusAdapter final : public sys::runtime::IBusAdapter
{
  public:
    SharedSpiBusAdapter(const char* owner_label, uint32_t owner_id)
        : owner_label_(owner_label), owner_id_(owner_id)
    {
    }

    bool tryAcquire(uint32_t timeout_ms) override
    {
        return shared_spi_lock_with_owner(pdMS_TO_TICKS(timeout_ms),
                                          owner_label_ && owner_label_[0] != '\0'
                                              ? owner_label_
                                              : "shared_runtime");
    }

    void release() override
    {
        shared_spi_unlock();
    }

    uint32_t nowMs() const override
    {
        return sys::millis_now();
    }

    uint32_t owner() const override
    {
        return owner_id_;
    }

  private:
    const char* owner_label_ = nullptr;
    uint32_t owner_id_ = 0;
};

class FixedSharedSpiBusPolicyStrategy final : public sys::runtime::BusPolicyStrategy
{
  public:
    FixedSharedSpiBusPolicyStrategy(uint32_t interactive_ms,
                                    uint32_t background_ms,
                                    uint32_t durable_ms,
                                    uint32_t recovery_ms)
        : interactive_ms_(interactive_ms),
          background_ms_(background_ms),
          durable_ms_(durable_ms),
          recovery_ms_(recovery_ms)
    {
    }

    sys::runtime::BusAccessPolicy select(
        const sys::runtime::RuntimeCommand& command) const override
    {
        if (command.priority == sys::runtime::RuntimePriority::Realtime ||
            command.priority == sys::runtime::RuntimePriority::Interactive)
        {
            return sys::runtime::BusAccessPolicy::InteractiveWorkerBounded;
        }
        if (command.kind == sys::runtime::RuntimeCommandKind::TrackStop ||
            command.kind == sys::runtime::RuntimeCommandKind::TrackFlush ||
            command.kind == sys::runtime::RuntimeCommandKind::PersistenceSave)
        {
            return sys::runtime::BusAccessPolicy::DurableCommit;
        }
        return sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    }

    uint32_t timeoutFor(sys::runtime::BusAccessPolicy policy) const override
    {
        switch (policy)
        {
        case sys::runtime::BusAccessPolicy::UiNeverBlock:
        case sys::runtime::BusAccessPolicy::DisplayFrameCritical:
            return 0;
        case sys::runtime::BusAccessPolicy::InteractiveWorkerBounded:
            return interactive_ms_;
        case sys::runtime::BusAccessPolicy::BackgroundWorkerBounded:
            return background_ms_;
        case sys::runtime::BusAccessPolicy::DurableCommit:
            return durable_ms_;
        case sys::runtime::BusAccessPolicy::RecoveryExclusive:
            return recovery_ms_;
        default:
            return 0;
        }
    }

  private:
    uint32_t interactive_ms_ = 0;
    uint32_t background_ms_ = 0;
    uint32_t durable_ms_ = 0;
    uint32_t recovery_ms_ = 0;
};

} // namespace platform::esp::common
