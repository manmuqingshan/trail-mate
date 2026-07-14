#pragma once

#if defined(ARDUINO)

#include <Arduino.h>

#else

#include "esp_random.h"

#include <cmath>
#include <cstdint>

namespace platform::esp::common::reticulum_runtime
{

class Console
{
  public:
    int printf(const char* format, ...);
    void println(const char* text);
};

Console& console();
std::uint32_t monotonic_millis();

} // namespace platform::esp::common::reticulum_runtime

#define Serial (::platform::esp::common::reticulum_runtime::console())
#define millis() (::platform::esp::common::reticulum_runtime::monotonic_millis())

#endif
