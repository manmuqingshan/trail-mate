#pragma once

#ifndef TRAIL_MATE_FIRMWARE_VERSION
#define TRAIL_MATE_FIRMWARE_VERSION ""
#endif

namespace platform::esp::common::build_info
{

inline const char* firmwareVersion()
{
    return TRAIL_MATE_FIRMWARE_VERSION;
}

} // namespace platform::esp::common::build_info
