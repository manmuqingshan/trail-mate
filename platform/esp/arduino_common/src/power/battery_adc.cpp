#include "platform/esp/arduino_common/power/battery_adc.h"

#include <Arduino.h>
#include <limits>

namespace platform::esp::arduino_common::power
{

int read_battery_adc_millivolts(uint8_t pin,
                                uint16_t divider_numerator,
                                uint16_t divider_denominator,
                                uint8_t samples)
{
    if (divider_denominator == 0)
    {
        return -1;
    }
    if (samples < 3)
    {
        samples = 3;
    }
    if (samples > 24)
    {
        samples = 24;
    }

    analogReadResolution(12);
    analogSetPinAttenuation(pin, ADC_11db);

    // Discard the first conversion after attenuation setup; ESP ADC readings
    // are noticeably less stable immediately after reconfiguration.
    (void)analogReadMilliVolts(pin);
    delayMicroseconds(250);

    uint32_t sum_mv = 0;
    int min_mv = std::numeric_limits<int>::max();
    int max_mv = 0;
    uint8_t valid = 0;

    for (uint8_t i = 0; i < samples; ++i)
    {
        const int mv = analogReadMilliVolts(pin);
        if (mv > 0)
        {
            sum_mv += static_cast<uint32_t>(mv);
            if (mv < min_mv)
            {
                min_mv = mv;
            }
            if (mv > max_mv)
            {
                max_mv = mv;
            }
            ++valid;
        }
        delayMicroseconds(250);
    }

    if (valid == 0)
    {
        return -1;
    }
    if (valid >= 3)
    {
        sum_mv -= static_cast<uint32_t>(min_mv);
        sum_mv -= static_cast<uint32_t>(max_mv);
        valid -= 2;
    }

    const uint32_t adc_mv = (sum_mv + (valid / 2U)) / valid;
    const uint32_t battery_mv =
        (adc_mv * divider_numerator + (divider_denominator / 2U)) / divider_denominator;
    if (battery_mv > static_cast<uint32_t>(std::numeric_limits<int>::max()))
    {
        return -1;
    }
    return static_cast<int>(battery_mv);
}

} // namespace platform::esp::arduino_common::power
