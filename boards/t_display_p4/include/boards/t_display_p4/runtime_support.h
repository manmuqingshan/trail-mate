#pragma once

#include <cstdint>

#include "boards/t_display_p4/board_profile.h"
#include "driver/i2c_master.h"

namespace boards::t_display_p4::runtime_support
{

struct SystemI2cDeviceConfig
{
    const char* owner = nullptr;
    uint16_t address = 0;
    uint32_t speed_hz = 400000;
};

const BoardProfile& profile();
DisplayPanelType configured_panel_type();
const BoardProfile::PanelGeometry& active_panel();
int touch_i2c_address();
bool reset_touch_controller(uint32_t pre_delay_ms = 10,
                            uint32_t reset_pulse_ms = 10,
                            uint32_t post_delay_ms = 10);
bool touch_interrupt_active();
bool lock_system_i2c(uint32_t timeout_ms = 1000);
void unlock_system_i2c();
i2c_master_dev_handle_t get_managed_system_i2c_device(const SystemI2cDeviceConfig& config,
                                                      uint32_t timeout_ms = 1000);

} // namespace boards::t_display_p4::runtime_support
