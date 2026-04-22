#include "boards/t_display_p4/runtime_support.h"

#include "boards/t_display_p4/t_display_p4_board.h"

namespace boards::t_display_p4::runtime_support
{

const BoardProfile& profile()
{
    return TDisplayP4Board::profile();
}

DisplayPanelType configured_panel_type()
{
    return TDisplayP4Board::configuredPanelType();
}

const BoardProfile::PanelGeometry& active_panel()
{
    return TDisplayP4Board::activePanel();
}

int touch_i2c_address()
{
    return TDisplayP4Board::touchI2cAddress();
}

bool reset_touch_controller(uint32_t pre_delay_ms, uint32_t reset_pulse_ms, uint32_t post_delay_ms)
{
    return TDisplayP4Board::instance().resetTouchController(pre_delay_ms, reset_pulse_ms, post_delay_ms);
}

bool touch_interrupt_active()
{
    return TDisplayP4Board::instance().isTouchInterruptActive();
}

bool lock_system_i2c(uint32_t timeout_ms)
{
    return TDisplayP4Board::instance().lockSystemI2c(timeout_ms);
}

void unlock_system_i2c()
{
    TDisplayP4Board::instance().unlockSystemI2c();
}

i2c_master_dev_handle_t get_managed_system_i2c_device(const SystemI2cDeviceConfig& config,
                                                      uint32_t timeout_ms)
{
    const TDisplayP4Board::SystemI2cDeviceConfig board_config{
        config.owner,
        config.address,
        config.speed_hz,
    };
    return TDisplayP4Board::instance().getManagedSystemI2cDevice(board_config, timeout_ms);
}

} // namespace boards::t_display_p4::runtime_support
