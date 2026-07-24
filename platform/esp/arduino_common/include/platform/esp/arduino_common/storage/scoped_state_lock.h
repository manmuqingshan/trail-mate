#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace platform::esp::arduino_common::storage
{

// Storage recovery owns this lock for bounded SD transactions. Every caller
// must fail fast instead of turning SD latency into a UI/radio stall.
constexpr TickType_t kStateLockWaitTicks = pdMS_TO_TICKS(50);

class ScopedRecursiveStateLock final
{
  public:
    explicit ScopedRecursiveStateLock(
        SemaphoreHandle_t mutex,
        TickType_t wait_ticks = kStateLockWaitTicks)
        : mutex_(mutex),
          locked_(mutex_ &&
                  xSemaphoreTakeRecursive(mutex_, wait_ticks) == pdTRUE)
    {
    }

    ~ScopedRecursiveStateLock()
    {
        if (locked_)
        {
            xSemaphoreGiveRecursive(mutex_);
        }
    }

    ScopedRecursiveStateLock(const ScopedRecursiveStateLock&) = delete;
    ScopedRecursiveStateLock& operator=(const ScopedRecursiveStateLock&) = delete;

    bool locked() const
    {
        return locked_;
    }

  private:
    SemaphoreHandle_t mutex_ = nullptr;
    bool locked_ = false;
};

} // namespace platform::esp::arduino_common::storage
