#include "platform/esp/idf_common/usb_console_runtime.h"

#include "sdkconfig.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
#include <cstdio>

#include "esp_err.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#endif

namespace platform::esp::idf_common::usb_console
{
namespace
{

bool s_started = false;

} // namespace

bool ensure_started()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
    if (s_started)
    {
        return true;
    }

    const tinyusb_config_t usb_config = {
        .device_descriptor = nullptr,
        .string_descriptor = nullptr,
        .external_phy = false,
#if TUD_OPT_HIGH_SPEED
        .fs_configuration_descriptor = nullptr,
        .hs_configuration_descriptor = nullptr,
        .qualifier_descriptor = nullptr,
#else
        .configuration_descriptor = nullptr,
#endif
    };
    if (tinyusb_driver_install(&usb_config) != ESP_OK)
    {
        return false;
    }

    const tinyusb_config_cdcacm_t cdc_config = {
        .usb_dev = TINYUSB_USBDEV_0,
        .cdc_port = TINYUSB_CDC_ACM_0,
        .rx_unread_buf_sz = CONFIG_TINYUSB_CDC_RX_BUFSIZE,
        .callback_rx = nullptr,
        .callback_rx_wanted_char = nullptr,
        .callback_line_state_changed = nullptr,
        .callback_line_coding_changed = nullptr,
    };
    if (tusb_cdc_acm_init(&cdc_config) != ESP_OK)
    {
        tinyusb_driver_uninstall();
        return false;
    }
    if (esp_tusb_init_console(TINYUSB_CDC_ACM_0) != ESP_OK)
    {
        (void)tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
        tinyusb_driver_uninstall();
        return false;
    }

    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    s_started = true;
    return true;
#else
    return true;
#endif
}

bool is_started()
{
    return s_started;
}

} // namespace platform::esp::idf_common::usb_console
