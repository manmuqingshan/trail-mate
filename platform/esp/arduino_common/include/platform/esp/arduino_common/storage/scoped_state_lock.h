#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace platform::esp::arduino_common::storage
{

class ScopedRecursiveStateLock final
{
  public:
    explicit ScopedRecursiveStateLock(SemaphoreHandle_t mutex)
        : mutex_(mutex),
          locked_(mutex_ &&
                  xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE)
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
