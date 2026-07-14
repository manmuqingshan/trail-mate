#include "platform/esp/common/reticulum_runtime_compat.h"

#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include "esp_timer.h"

#include <cstdarg>
#include <cstdio>

namespace platform::esp::common::reticulum_runtime
{
namespace
{

Console s_console{};

} // namespace

int Console::printf(const char* format, ...)
{
    if (!format)
    {
        return 0;
    }

    va_list args;
    va_start(args, format);
    const int written = std::vprintf(format, args);
    va_end(args);
    return written;
}

void Console::println(const char* text)
{
    std::puts(text ? text : "");
}

Console& console()
{
    return s_console;
}

std::uint32_t monotonic_millis()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

} // namespace platform::esp::common::reticulum_runtime

#endif
