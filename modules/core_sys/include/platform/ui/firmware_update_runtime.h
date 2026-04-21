#pragma once

#include <cstdint>

namespace platform::ui::firmware_update
{

enum class Phase : uint8_t
{
    Unsupported = 0,
    Idle,
    Checking,
    UpToDate,
    UpdateAvailable,
    Downloading,
    Installing,
    Rebooting,
    Error,
};

struct Status
{
    bool supported = false;
    bool busy = false;
    bool checked = false;
    bool update_available = false;
    bool direct_ota = false;
    int progress_percent = -1;
    Phase phase = Phase::Unsupported;
    char current_version[24] = {};
    char latest_version[24] = {};
    char message[96] = {};
    char detail[96] = {};
};

bool is_supported();
Status status();
bool start_check();
bool start_install();

} // namespace platform::ui::firmware_update
