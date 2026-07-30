#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace platform::esp::arduino_common::storage
{

// This lock protects logical in-memory state only. Filesystem and device
// transactions must happen before or after its lifetime.
constexpr TickType_t kStateLockWaitTicks = pdMS_TO_TICKS(50);

enum class StateLockResult : uint8_t
{
    Acquired = 0,
    Busy,
    Unavailable,
};

class ScopedRecursiveStateLock final
{
  public:
    explicit ScopedRecursiveStateLock(
        SemaphoreHandle_t mutex,
        TickType_t wait_ticks = kStateLockWaitTicks)
        : mutex_(mutex)
    {
        if (!mutex_)
        {
            result_ = StateLockResult::Unavailable;
        }
        else if (xSemaphoreTakeRecursive(mutex_, wait_ticks) == pdTRUE)
        {
            result_ = StateLockResult::Acquired;
        }
        else
        {
            result_ = StateLockResult::Busy;
        }
    }

    ~ScopedRecursiveStateLock()
    {
        if (result_ == StateLockResult::Acquired)
        {
            xSemaphoreGiveRecursive(mutex_);
        }
    }

    ScopedRecursiveStateLock(const ScopedRecursiveStateLock&) = delete;
    ScopedRecursiveStateLock& operator=(const ScopedRecursiveStateLock&) = delete;

    bool locked() const
    {
        return result_ == StateLockResult::Acquired;
    }
    StateLockResult result() const { return result_; }

  private:
    SemaphoreHandle_t mutex_ = nullptr;
    StateLockResult result_ = StateLockResult::Unavailable;
};

} // namespace platform::esp::arduino_common::storage
