#pragma once

#include <cstdint>

namespace platform::esp::arduino_common::power
{

int read_battery_adc_millivolts(uint8_t pin,
                                uint16_t divider_numerator = 2,
                                uint16_t divider_denominator = 1,
                                uint8_t samples = 12);

} // namespace platform::esp::arduino_common::power
