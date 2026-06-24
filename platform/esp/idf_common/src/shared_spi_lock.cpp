#include "platform/esp/common/shared_spi_lock.h"

namespace platform::esp::common
{

bool shared_spi_lock(TickType_t xTicksToWait)
{
    (void)xTicksToWait;
    return true;
}

bool shared_spi_lock_with_owner(TickType_t xTicksToWait, const char* owner)
{
    (void)xTicksToWait;
    (void)owner;
    return true;
}

void shared_spi_unlock() {}

void note_display_spi_timeout(uint32_t now_ms)
{
    (void)now_ms;
}

uint32_t last_display_spi_timeout_ms()
{
    return 0;
}

bool display_spi_recently_timed_out(uint32_t now_ms, uint32_t window_ms)
{
    (void)now_ms;
    (void)window_ms;
    return false;
}

} // namespace platform::esp::common
