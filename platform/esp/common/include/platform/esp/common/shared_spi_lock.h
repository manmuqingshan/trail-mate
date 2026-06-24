#pragma once

#include "freertos/FreeRTOS.h"
#include <stdint.h>

namespace platform::esp::common
{

// The shared SPI lock arbitrates ownership of the board-level SPI bus on
// devices where display, SD, radio, NFC, or other peripherals physically
// share one controller.
bool shared_spi_lock(TickType_t xTicksToWait);
bool shared_spi_lock_with_owner(TickType_t xTicksToWait, const char* owner);
void shared_spi_unlock();
void note_display_spi_timeout(uint32_t now_ms);
uint32_t last_display_spi_timeout_ms();
bool display_spi_recently_timed_out(uint32_t now_ms, uint32_t window_ms);

class SharedSpiLockGuard
{
  public:
    explicit SharedSpiLockGuard(TickType_t wait_ticks, const char* owner = nullptr)
        : locked_(owner ? shared_spi_lock_with_owner(wait_ticks, owner)
                        : shared_spi_lock(wait_ticks))
    {
    }

    SharedSpiLockGuard(const SharedSpiLockGuard&) = delete;
    SharedSpiLockGuard& operator=(const SharedSpiLockGuard&) = delete;
    SharedSpiLockGuard(SharedSpiLockGuard&&) = delete;
    SharedSpiLockGuard& operator=(SharedSpiLockGuard&&) = delete;

    ~SharedSpiLockGuard()
    {
        if (locked_)
        {
            shared_spi_unlock();
        }
    }

    bool locked() const { return locked_; }

  private:
    bool locked_ = false;
};

} // namespace platform::esp::common
