#pragma once

#include <cstdint>

namespace platform::ui::compass
{

struct CompassState
{
    bool supported = false;
    bool available = false;
    bool heading_valid = false;
    bool magnetic_ok = false;
    bool flat = false;
    bool calibrated = false;
    float heading_deg = 0.0F;
    uint8_t calibration_percent = 0;
    uint32_t age_ms = 0xFFFFFFFFUL;
};

CompassState get_state();

} // namespace platform::ui::compass
