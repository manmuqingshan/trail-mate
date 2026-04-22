#pragma once

#include <ctime>

#include "boards/t_display_p4/rtc_runtime.h"
#include "boards/t_display_p4/t_display_p4_board.h"
#include "esp_log.h"
#include "platform/esp/boards/board_runtime.h"

extern "C"
{
#include "bsp/trail_mate_t_display_p4_runtime.h"
}

namespace platform::esp::boards::detail
{

namespace
{
constexpr const char* kTag = "t-display-p4-board-runtime";
}

inline void initializeBoard(bool waking_from_sleep)
{
    const auto& profile = ::boards::t_display_p4::TDisplayP4Board::profile();
    ESP_LOGI(kTag,
             "T-Display-P4 board runtime bootstrap: caps(display=%d touch=%d audio=%d sd=%d gps=%d lora=%d ioexp_lora=%d) "
             "sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d) sdmmc=(%d,%d,%d,%d,%d,%d) "
             "audio_i2s=(%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d rst=%d irq=%d busy=%d pwr_en=%d) "
             "boot=%d expander_int=%d backlight=%d wake=%d",
             profile.has_display ? 1 : 0,
             profile.has_touch ? 1 : 0,
             profile.has_audio ? 1 : 0,
             profile.has_sdcard ? 1 : 0,
             profile.has_gps_uart ? 1 : 0,
             profile.has_lora ? 1 : 0,
             profile.uses_io_expander_for_lora ? 1 : 0,
             profile.sys_i2c.port,
             profile.sys_i2c.sda,
             profile.sys_i2c.scl,
             profile.ext_i2c.port,
             profile.ext_i2c.sda,
             profile.ext_i2c.scl,
             profile.gps_uart.port,
             profile.gps_uart.tx,
             profile.gps_uart.rx,
             profile.sdmmc.d0,
             profile.sdmmc.d1,
             profile.sdmmc.d2,
             profile.sdmmc.d3,
             profile.sdmmc.cmd,
             profile.sdmmc.clk,
             profile.audio_i2s.bclk,
             profile.audio_i2s.mclk,
             profile.audio_i2s.ws,
             profile.audio_i2s.dout,
             profile.audio_i2s.din,
             profile.lora.spi.host,
             profile.lora.spi.sck,
             profile.lora.spi.miso,
             profile.lora.spi.mosi,
             profile.lora.nss,
             profile.lora.rst,
             profile.lora.irq,
             profile.lora.busy,
             profile.lora.pwr_en,
             profile.boot,
             profile.expander_int,
             profile.lcd_backlight,
             waking_from_sleep ? 1 : 0);

    (void)::boards::t_display_p4::TDisplayP4Board::instance().begin();
}

inline void initializeDisplay()
{
    if (trail_mate_t_display_p4_display_runtime_init())
    {
        ESP_LOGI(kTag, "T-Display-P4 display runtime initialized");
    }
    else
    {
        ESP_LOGE(kTag, "T-Display-P4 display runtime initialization failed");
    }
}

inline bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    if (!out_handles)
    {
        return false;
    }

    *out_handles = resolveAppContextInitHandles();
    return out_handles->isValid();
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
    return {&::boards::t_display_p4::TDisplayP4Board::instance(),
            &::boards::t_display_p4::TDisplayP4Board::instance(),
            nullptr,
            nullptr};
}

inline bool lockDisplay(uint32_t timeout_ms)
{
    return trail_mate_t_display_p4_display_lock(timeout_ms);
}

inline void unlockDisplay()
{
    trail_mate_t_display_p4_display_unlock();
}

inline bool syncSystemTimeFromBoardRtc()
{
    return ::boards::t_display_p4::rtc_runtime::sync_system_time_from_hardware_rtc();
}

inline bool applySystemTimeAndSyncBoardRtc(std::time_t epoch_seconds, const char* source)
{
    return ::boards::t_display_p4::rtc_runtime::apply_system_time_and_sync_rtc(epoch_seconds, source);
}

inline BoardIdentity defaultIdentity()
{
    return {::boards::t_display_p4::TDisplayP4Board::defaultLongName(),
            ::boards::t_display_p4::TDisplayP4Board::defaultShortName(),
            ::boards::t_display_p4::TDisplayP4Board::defaultBleName()};
}

} // namespace platform::esp::boards::detail
