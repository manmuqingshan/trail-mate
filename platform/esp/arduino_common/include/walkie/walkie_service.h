#pragma once

#include <cstdint>

namespace walkie
{
struct Status
{
    bool active = false;
    bool tx = false;
    bool monitor_enabled = false;
    uint8_t tx_level = 0;
    uint8_t rx_level = 0;
    float freq_mhz = 0.0f;
};

bool start();
void stop();
bool is_active();
void set_ptt(bool pressed);
bool set_monitor_enabled(bool enabled);
bool is_monitor_enabled();
int get_volume();
void on_key_event(char key, int state);
Status get_status();
const char* get_last_error();
} // namespace walkie
