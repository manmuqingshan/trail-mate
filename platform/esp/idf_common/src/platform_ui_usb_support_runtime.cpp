#include "platform/ui/usb_support_runtime.h"

#include "sdkconfig.h"

#include <cstdio>

#if (defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)) && \
    defined(CONFIG_TINYUSB_MSC_ENABLED)
#define TRAILMATE_IDF_USB_MSC_BACKEND 1
#else
#define TRAILMATE_IDF_USB_MSC_BACKEND 0
#endif

#if TRAILMATE_IDF_USB_MSC_BACKEND
#include "app/app_facade_access.h"
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/tab5_board.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/t_display_p4_board.h"
#include "platform/esp/idf_common/usb_console_runtime.h"
#endif
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/sdmmc_host_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "screen_sleep.h"
#include "sdmmc_cmd.h"
#include "team/usecase/team_pairing_service.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#endif

namespace platform::ui::usb_support
{
namespace
{

Status s_status{};
char s_message[96] = "";
bool s_prepared = false;

void set_status_message(const char* message)
{
    const char* source = (message && message[0] != '\0') ? message : "";
    std::snprintf(s_message, sizeof(s_message), "%s", source);
    s_status.message = s_message;
}

void stop_pairing()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    if (team::TeamPairingService* pairing = app::teamFacade().getTeamPairing())
    {
        pairing->stop();
    }
#endif
}

#if TRAILMATE_IDF_USB_MSC_BACKEND
constexpr const char* kUsbVendor = "TrailMate";
constexpr const char* kUsbProduct = "USB Disk";
constexpr const char* kUsbSerial = "TM-IDF";
constexpr uint8_t kInterfaceMsc = 0;
constexpr uint8_t kInterfaceTotal = 1;
constexpr uint8_t kEndpointMscOut = 0x01;
constexpr uint8_t kEndpointMscIn = 0x81;
constexpr uint16_t kUsbConfigDescLen = TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN;

bool s_usb_installed = false;
bool s_storage_initialized = false;
sdmmc_card_t* s_card = nullptr;
sdmmc_host_t s_sd_host = SDMMC_HOST_DEFAULT();

static tusb_desc_device_t s_device_descriptor = {
    .bLength = sizeof(s_device_descriptor),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static char const* s_string_descriptors[] = {
    (const char[]){0x09, 0x04},
    kUsbVendor,
    kUsbProduct,
    kUsbSerial,
    "MSC",
};

static uint8_t const s_msc_fs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, kInterfaceTotal, 0, kUsbConfigDescLen, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(kInterfaceMsc, 0, kEndpointMscOut, kEndpointMscIn, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t s_device_qualifier = {
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

static uint8_t const s_msc_hs_configuration_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, kInterfaceTotal, 0, kUsbConfigDescLen, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(kInterfaceMsc, 0, kEndpointMscOut, kEndpointMscIn, 512),
};
#endif

bool unmount_application_sd()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    const bool ok = ::boards::t_display_p4::TDisplayP4Board::instance().unmountSdCard();
    if (ok)
    {
        platform::esp::idf_common::bsp_runtime::mark_sdcard_unmounted();
    }
    return ok;
#else
    ::platform::esp::arduino_common::storage::unmount_sd_card();
    platform::esp::idf_common::bsp_runtime::mark_sdcard_unmounted();
    return true;
#endif
}

bool remount_application_sd()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::instance().mountSdCard(
        platform::esp::idf_common::bsp_runtime::sdcard_mount_point(), 8);
#else
    return platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
#endif
}

void refresh_runtime_message()
{
    if (!s_status.active)
    {
        return;
    }

    if (tinyusb_msc_storage_in_use_by_usb_host())
    {
        set_status_message("USB host connected");
    }
    else
    {
        set_status_message("Waiting for host...");
    }
}

void storage_mount_changed_cb(tinyusb_msc_event_t* event)
{
    if (!event)
    {
        return;
    }

    if (event->mount_changed_data.is_mounted)
    {
        set_status_message("Storage mounted to app");
    }
    else
    {
        set_status_message("USB host connected");
    }
}

void deinit_sd_host()
{
    if (s_card)
    {
        (void)platform::esp::idf_common::sdmmc_host_runtime::release_slot(
            platform::esp::idf_common::sdmmc_host_runtime::SlotOwner::UsbMassStorage,
            s_sd_host.slot);
        free(s_card);
        s_card = nullptr;
    }
}

esp_err_t init_sd_host_raw()
{
    if (s_card)
    {
        return ESP_OK;
    }

    s_sd_host = SDMMC_HOST_DEFAULT();
    s_sd_host.slot = SDMMC_HOST_SLOT_0;
    s_sd_host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (!::boards::t_display_p4::TDisplayP4Board::instance().ensureExternal3v3Power())
    {
        return ESP_FAIL;
    }
#endif

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    const auto& pins = ::boards::tab5::Tab5Board::sdmmcPins();
#else
    const auto& pins = ::boards::t_display_p4::TDisplayP4Board::sdmmcPins();
#endif
    slot_config.clk = static_cast<gpio_num_t>(pins.clk);
    slot_config.cmd = static_cast<gpio_num_t>(pins.cmd);
    slot_config.d0 = static_cast<gpio_num_t>(pins.d0);
    slot_config.d1 = static_cast<gpio_num_t>(pins.d1);
    slot_config.d2 = static_cast<gpio_num_t>(pins.d2);
    slot_config.d3 = static_cast<gpio_num_t>(pins.d3);
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
#endif

    s_card = static_cast<sdmmc_card_t*>(malloc(sizeof(sdmmc_card_t)));
    if (!s_card)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = platform::esp::idf_common::sdmmc_host_runtime::initialize_slot(
        platform::esp::idf_common::sdmmc_host_runtime::SlotOwner::UsbMassStorage,
        s_sd_host,
        slot_config);
    if (err != ESP_OK)
    {
        deinit_sd_host();
        return err;
    }

    err = sdmmc_card_init(&s_sd_host, s_card);
    if (err != ESP_OK)
    {
        deinit_sd_host();
        return err;
    }

    return ESP_OK;
}

bool start_backend()
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        set_status_message("SD card not ready");
        return false;
    }

    if (!unmount_application_sd())
    {
        set_status_message("SD unmount failed");
        return false;
    }

    const esp_err_t sd_err = init_sd_host_raw();
    if (sd_err != ESP_OK)
    {
        set_status_message("Raw SD init failed");
        (void)remount_application_sd();
        return false;
    }

    esp_vfs_fat_mount_config_t mount_cfg = VFS_FAT_MOUNT_DEFAULT_CONFIG();
    mount_cfg.max_files = 8;
    tinyusb_msc_sdmmc_config_t config_sdmmc = {
        s_card,
        storage_mount_changed_cb,
        nullptr,
        mount_cfg,
    };
    if (tinyusb_msc_storage_init_sdmmc(&config_sdmmc) != ESP_OK)
    {
        set_status_message("USB storage init failed");
        deinit_sd_host();
        (void)remount_application_sd();
        return false;
    }
    s_storage_initialized = true;
    (void)tinyusb_msc_register_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED, storage_mount_changed_cb);

    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &s_device_descriptor,
        .string_descriptor = s_string_descriptors,
        .string_descriptor_count = sizeof(s_string_descriptors) / sizeof(s_string_descriptors[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = s_msc_fs_configuration_desc,
        .hs_configuration_descriptor = s_msc_hs_configuration_desc,
        .qualifier_descriptor = &s_device_qualifier,
#else
        .configuration_descriptor = s_msc_fs_configuration_desc,
#endif
        .self_powered = false,
        .vbus_monitor_io = GPIO_NUM_NC,
    };
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    platform::esp::idf_common::usb_console::stop();
#endif
    if (tinyusb_driver_install(&tusb_cfg) != ESP_OK)
    {
        (void)tinyusb_msc_unregister_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED);
        tinyusb_msc_storage_deinit();
        s_storage_initialized = false;
        deinit_sd_host();
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
        (void)platform::esp::idf_common::usb_console::ensure_started();
#endif
        (void)remount_application_sd();
        set_status_message("TinyUSB install failed");
        return false;
    }

    s_usb_installed = true;
    s_status.active = true;
    refresh_runtime_message();
    return true;
}

void stop_backend()
{
    if (s_usb_installed)
    {
        tinyusb_driver_uninstall();
        s_usb_installed = false;
    }

    if (s_storage_initialized)
    {
        (void)tinyusb_msc_unregister_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED);
        tinyusb_msc_storage_deinit();
        s_storage_initialized = false;
    }

    deinit_sd_host();
    (void)remount_application_sd();
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    (void)platform::esp::idf_common::usb_console::ensure_started();
#endif
}
#endif

} // namespace

bool is_supported()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    return true;
#else
    return false;
#endif
}

void prepare_mass_storage_mode()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    stop_pairing();
    disableScreenSleep();

    platform::ui::gps::suspend_runtime();
#endif
}

void restore_mass_storage_mode()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    enableScreenSleep();

    platform::ui::gps::resume_runtime();
#endif
}

bool start()
{
    s_status = Status{};
    s_status.message = s_message;
    s_status.stop_requested = false;

    if (!is_supported())
    {
        set_status_message("USB MSC is unavailable on this IDF target");
        return false;
    }

#if TRAILMATE_IDF_USB_MSC_BACKEND
    if (s_status.active)
    {
        refresh_runtime_message();
        return true;
    }

    prepare_mass_storage_mode();
    s_prepared = true;
    const bool ok = start_backend();
    if (!ok)
    {
        restore_mass_storage_mode();
        s_prepared = false;
        s_status.active = false;
        return false;
    }
    s_status.active = true;
    refresh_runtime_message();
    return true;
#else
    return false;
#endif
}

void stop()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    if (s_status.active)
    {
        stop_backend();
    }
#endif

    if (s_prepared)
    {
        restore_mass_storage_mode();
        s_prepared = false;
    }

    s_status.active = false;
    s_status.stop_requested = false;
    set_status_message("USB Stopped");
}

Status get_status()
{
#if TRAILMATE_IDF_USB_MSC_BACKEND
    if (s_status.active)
    {
        refresh_runtime_message();
    }
#endif
    s_status.message = s_message;
    return s_status;
}

} // namespace platform::ui::usb_support
