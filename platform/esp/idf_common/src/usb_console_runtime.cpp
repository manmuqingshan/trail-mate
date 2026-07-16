#include "platform/esp/idf_common/usb_console_runtime.h"

#include "sdkconfig.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_err.h"
#include "tinyusb.h"
#include "tusb.h"
#include "tusb_cdc_acm.h"
#include "tusb_console.h"
#endif

namespace platform::esp::idf_common::usb_console
{
namespace
{

bool s_started = false;

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
constexpr uint8_t kInterfaceCdc = 0;
constexpr uint8_t kInterfaceCdcData = 1;
constexpr uint8_t kInterfaceTotal = 2;
constexpr uint8_t kEndpointCdcNotification = 0x81;
constexpr uint8_t kEndpointCdcOut = 0x02;
constexpr uint8_t kEndpointCdcIn = 0x82;
constexpr uint16_t kUsbConfigDescLen = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN;

const tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4001,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

const char* s_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Trail Mate",
    "Trail Mate T-Display-P4",
    "TM-P4",
    "Trail Mate Console",
};

const uint8_t s_cdc_fs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          kInterfaceTotal,
                          0,
                          kUsbConfigDescLen,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          100),
    TUD_CDC_DESCRIPTOR(kInterfaceCdc,
                       4,
                       kEndpointCdcNotification,
                       8,
                       kEndpointCdcOut,
                       kEndpointCdcIn,
                       64),
};

#if TUD_OPT_HIGH_SPEED
const tusb_desc_device_qualifier_t s_device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved = 0,
};

const uint8_t s_cdc_hs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          kInterfaceTotal,
                          0,
                          kUsbConfigDescLen,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          100),
    TUD_CDC_DESCRIPTOR(kInterfaceCdc,
                       4,
                       kEndpointCdcNotification,
                       8,
                       kEndpointCdcOut,
                       kEndpointCdcIn,
                       512),
};
#endif
#endif

} // namespace

bool ensure_started()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
    if (s_started)
    {
        return true;
    }

    const tinyusb_config_t usb_config = {
        .device_descriptor = &s_device_descriptor,
        .string_descriptor = s_string_descriptors,
        .string_descriptor_count = sizeof(s_string_descriptors) /
                                   sizeof(s_string_descriptors[0]),
        .external_phy = false,
#if TUD_OPT_HIGH_SPEED
        .fs_configuration_descriptor = s_cdc_fs_configuration_desc,
        .hs_configuration_descriptor = s_cdc_hs_configuration_desc,
        .qualifier_descriptor = &s_device_qualifier,
#else
        .configuration_descriptor = s_cdc_fs_configuration_desc,
#endif
        .self_powered = false,
        .vbus_monitor_io = GPIO_NUM_NC,
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

void stop()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) && defined(CONFIG_TINYUSB_CDC_ENABLED)
    if (!s_started)
    {
        return;
    }
    (void)esp_tusb_deinit_console(TINYUSB_CDC_ACM_0);
    (void)tusb_cdc_acm_deinit(TINYUSB_CDC_ACM_0);
    tinyusb_driver_uninstall();
    s_started = false;
#endif
}

} // namespace platform::esp::idf_common::usb_console
