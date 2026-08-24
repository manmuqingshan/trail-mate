#pragma once

namespace platform::ui::usb_support
{

struct Status
{
    bool active = false;
    bool stop_requested = false;
    const char* message = "";
};

bool is_supported();
bool start();
void stop();
void report_previous_exit_trace();
Status get_status();
bool prepare_mass_storage_mode();
void restore_mass_storage_mode();

} // namespace platform::ui::usb_support
