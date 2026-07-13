#include "platform/ui/usb_support_runtime.h"

#include "app/app_facade_access.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_bus_arbiter.h"
#include "platform/ui/device_runtime.h"
#include "screen_sleep.h"
#include "sys/bus_access_scope.h"
#include "sys/clock.h"
#include "team/usecase/team_pairing_service.h"

#include <cstdio>

#if defined(ARDUINO_USB_MODE)
#include <USB.h>
#include <USBMSC.h>
#include <esp_event.h>

#include <cstring>
#endif

namespace platform::ui::usb_support
{
namespace
{

Status s_status{};
char s_message[96] = "";
bool s_prepared = false;
bool s_radio_tasks_paused_by_usb = false;

#define USB_MSC_LOG(...) std::printf("[USBMSC] " __VA_ARGS__)

void set_status_message(const char* message)
{
    const char* source = (message && message[0] != '\0') ? message : "";
    std::snprintf(s_message, sizeof(s_message), "%s", source);
    s_status.message = s_message;
}

void stop_pairing()
{
    if (team::TeamPairingService* pairing = app::teamFacade().getTeamPairing())
    {
        pairing->stop();
    }
}

#if defined(ARDUINO_USB_MODE)
USBMSC s_msc;
bool s_backend_started = false;

constexpr uint32_t kUsbMscSectorWaitMs = 50;
constexpr uint32_t kUsbMscSessionWaitMs = 250;
constexpr uint32_t kUsbMscBusResource = 7;
constexpr uint32_t kUsbMscBusOwnerId = 0x555342u; // 'USB'
constexpr const char* kUsbMscBusOwner = "usb_msc_sd";

::platform::esp::common::SharedSpiBusAdapter s_usb_msc_bus_adapter(
    kUsbMscBusOwner,
    kUsbMscBusOwnerId);
::platform::esp::common::FixedSharedSpiBusPolicyStrategy s_usb_msc_bus_policy(
    kUsbMscSectorWaitMs,
    kUsbMscSectorWaitMs,
    kUsbMscSessionWaitMs,
    kUsbMscSessionWaitMs);
sys::runtime::StorageBusArbiter s_usb_msc_bus_arbiter(s_usb_msc_bus_adapter,
                                                      s_usb_msc_bus_policy);

enum class UsbMscBusCommand : uint8_t
{
    SectorRead = 1,
    SectorWrite,
    SessionStart,
};

class UsbMscBusGate final
{
  public:
    UsbMscBusGate(UsbMscBusCommand command,
                  sys::runtime::BusAccessPolicy policy,
                  uint32_t wait_ms)
        : scope_(s_usb_msc_bus_arbiter, makeRequest(command, policy, wait_ms))
    {
    }

    bool locked() const
    {
        return scope_.acquired();
    }

  private:
    static sys::runtime::BusAcquireRequest makeRequest(
        UsbMscBusCommand command,
        sys::runtime::BusAccessPolicy policy,
        uint32_t wait_ms)
    {
        sys::runtime::BusAcquireRequest request{};
        request.resource = kUsbMscBusResource;
        request.policy = policy;
        request.command_id = kUsbMscBusOwnerId + static_cast<uint32_t>(command);
        request.origin = kUsbMscBusOwnerId;
        request.deadline_ms = sys::millis_now() + wait_ms;
        return request;
    }

    sys::runtime::ScopedBusAccessToken scope_;
};

class UsbMscStorageSession final
{
  public:
    bool begin()
    {
        if (active_)
        {
            return true;
        }

        UsbMscBusGate gate(UsbMscBusCommand::SessionStart,
                           sys::runtime::BusAccessPolicy::RecoveryExclusive,
                           kUsbMscSessionWaitMs);
        if (!gate.locked())
        {
            return false;
        }

        ::platform::esp::arduino_common::storage::sd_set_external_block_owner_active(
            true);
        active_ = true;
        return true;
    }

    void end()
    {
        if (!active_)
        {
            return;
        }
        ::platform::esp::arduino_common::storage::sd_set_external_block_owner_active(
            false);
        active_ = false;
    }

    bool active() const
    {
        return active_;
    }

  private:
    bool active_ = false;
};

UsbMscStorageSession s_storage_session;
uint8_t s_usb_msc_sector_scratch[512];

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
    UsbMscBusGate bus_gate(UsbMscBusCommand::SectorRead,
                           sys::runtime::BusAccessPolicy::RecoveryExclusive,
                           kUsbMscSectorWaitMs);
    if (!bus_gate.locked())
    {
        USB_MSC_LOG("read lock failed lba=%lu size=%lu\n",
                    static_cast<unsigned long>(lba),
                    static_cast<unsigned long>(bufsize));
        return -1;
    }

    const uint32_t sec_size =
        ::platform::esp::arduino_common::storage::sd_card_info().sector_size;
    if (sec_size == 0)
    {
        return -1;
    }
    if (sec_size > 512)
    {
        return -1;
    }

    const uint32_t lba_delta = offset / sec_size;
    if (lba_delta > (0xFFFFFFFFu - lba))
    {
        return -1;
    }

    uint32_t current_lba = lba + lba_delta;
    uint32_t sector_offset = offset % sec_size;
    uint8_t* out = reinterpret_cast<uint8_t*>(buffer);
    uint32_t remaining = bufsize;

    while (remaining > 0)
    {
        const uint32_t capacity = sec_size - sector_offset;
        const uint32_t chunk = remaining < capacity ? remaining : capacity;
        if (sector_offset == 0 && chunk == sec_size)
        {
            if (!::platform::esp::arduino_common::storage::sd_read_raw(current_lba, out))
            {
                return -1;
            }
        }
        else
        {
            if (!::platform::esp::arduino_common::storage::sd_read_raw(current_lba,
                                                                       s_usb_msc_sector_scratch))
            {
                return -1;
            }
            std::memcpy(out, s_usb_msc_sector_scratch + sector_offset, chunk);
        }

        out += chunk;
        remaining -= chunk;
        ++current_lba;
        sector_offset = 0;
    }

    return static_cast<int32_t>(bufsize);
}

int32_t usbWriteCallback(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
    UsbMscBusGate bus_gate(UsbMscBusCommand::SectorWrite,
                           sys::runtime::BusAccessPolicy::RecoveryExclusive,
                           kUsbMscSectorWaitMs);
    if (!bus_gate.locked())
    {
        USB_MSC_LOG("write lock failed lba=%lu size=%lu\n",
                    static_cast<unsigned long>(lba),
                    static_cast<unsigned long>(bufsize));
        return -1;
    }

    // USB MSC writes raw sectors, including filesystem metadata and already
    // allocated blocks. SdFat free-space accounting is not valid at this layer.
    const uint32_t sec_size =
        ::platform::esp::arduino_common::storage::sd_card_info().sector_size;
    if (sec_size == 0)
    {
        return -1;
    }
    if (sec_size > 512)
    {
        return -1;
    }

    const uint32_t lba_delta = offset / sec_size;
    if (lba_delta > (0xFFFFFFFFu - lba))
    {
        return -1;
    }

    uint32_t current_lba = lba + lba_delta;
    uint32_t sector_offset = offset % sec_size;
    uint8_t* in = buffer;
    uint32_t remaining = bufsize;

    while (remaining > 0)
    {
        const uint32_t capacity = sec_size - sector_offset;
        const uint32_t chunk = remaining < capacity ? remaining : capacity;
        if (sector_offset == 0 && chunk == sec_size)
        {
            if (!::platform::esp::arduino_common::storage::sd_write_raw(current_lba, in))
            {
                return -1;
            }
        }
        else
        {
            if (!::platform::esp::arduino_common::storage::sd_read_raw(current_lba,
                                                                       s_usb_msc_sector_scratch))
            {
                return -1;
            }
            std::memcpy(s_usb_msc_sector_scratch + sector_offset, in, chunk);
            if (!::platform::esp::arduino_common::storage::sd_write_raw(current_lba,
                                                                        s_usb_msc_sector_scratch))
            {
                return -1;
            }
        }

        in += chunk;
        remaining -= chunk;
        ++current_lba;
        sector_offset = 0;
    }

    return static_cast<int32_t>(bufsize);
}

bool usbStartStopCallback(uint8_t power_condition, bool start, bool load_eject)
{
    (void)power_condition;
    if (!start && load_eject)
    {
        s_status.stop_requested = true;
        set_status_message("USB host requested eject");
        return false;
    }

    return true;
}

bool setup_usb_msc()
{
    if (!platform::ui::device::card_ready())
    {
        set_status_message("SD Card Not Found");
        return false;
    }

    if (!::platform::esp::arduino_common::storage::sd_card_ready())
    {
        set_status_message("SD Card Not Detected");
        return false;
    }

    const auto card_info = ::platform::esp::arduino_common::storage::sd_card_info();
    const uint32_t sec_size = card_info.sector_size;
    if (sec_size == 0)
    {
        set_status_message("SD Card Sector Error");
        return false;
    }

    const uint64_t card_size = card_info.card_size_bytes;
    if (card_size == 0)
    {
        set_status_message("SD Card Not Ready");
        return false;
    }

    const uint32_t num_sectors = static_cast<uint32_t>(card_size / sec_size);
    s_msc.vendorID("TrailMate");
    s_msc.productID("SD Card");
    s_msc.productRevision("1.0");
    s_msc.onRead(usbReadCallback);
    s_msc.onWrite(usbWriteCallback);
    s_msc.onStartStop(usbStartStopCallback);
    s_msc.mediaPresent(true);
    if (!s_msc.begin(num_sectors, sec_size))
    {
        set_status_message("USB MSC Init Failed");
        return false;
    }

    USB.onEvent([](void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
                {
        (void)arg;
        (void)event_data;
        if (event_base != ARDUINO_USB_EVENTS)
        {
            return;
        }

        switch (event_id)
        {
        case ARDUINO_USB_STARTED_EVENT:
            set_status_message("USB Started - Ready");
            break;
        case ARDUINO_USB_STOPPED_EVENT:
            set_status_message("USB Stopped");
            break;
        case ARDUINO_USB_SUSPEND_EVENT:
            set_status_message("USB Suspended");
            break;
        case ARDUINO_USB_RESUME_EVENT:
            set_status_message("USB Resumed");
            break;
        default:
            break;
        } });

    if (!USB.begin())
    {
        s_msc.end();
        set_status_message("USB Stack Init Failed");
        return false;
    }

    set_status_message("Initializing USB...");
    s_backend_started = true;
    return true;
}
#endif

} // namespace

bool is_supported()
{
#if defined(ARDUINO_USB_MODE)
    return true;
#else
    return false;
#endif
}

void prepare_mass_storage_mode()
{
    stop_pairing();
    esp_wifi_stop();
    disableScreenSleep();

    if (!app::AppTasks::areRadioTasksPaused())
    {
        app::AppTasks::pauseRadioTasks();
        s_radio_tasks_paused_by_usb = true;
        USB_MSC_LOG("radio tasks paused for USB mass storage\n");
    }

    TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
    if (gps_task_handle != nullptr)
    {
        vTaskSuspend(gps_task_handle);
    }
}

void restore_mass_storage_mode()
{
    enableScreenSleep();

    TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
    if (gps_task_handle != nullptr)
    {
        vTaskResume(gps_task_handle);
    }

    if (s_radio_tasks_paused_by_usb)
    {
        app::AppTasks::resumeRadioTasks();
        s_radio_tasks_paused_by_usb = false;
        USB_MSC_LOG("radio tasks resumed after USB mass storage\n");
    }
}

bool start()
{
    s_status = Status{};
    s_status.message = s_message;
    s_status.stop_requested = false;

    if (!is_supported())
    {
        set_status_message("USB is unavailable on this target");
        return false;
    }

#if defined(ARDUINO_USB_MODE)
    if (s_backend_started)
    {
        if (!s_storage_session.active() && !s_storage_session.begin())
        {
            set_status_message("SD busy");
            s_status.active = false;
            return false;
        }
        s_status.active = true;
        return true;
    }

    if (!platform::ui::device::card_ready())
    {
        set_status_message("SD Card Not Found");
        return false;
    }

    prepare_mass_storage_mode();
    s_prepared = true;
    if (!s_storage_session.begin())
    {
        restore_mass_storage_mode();
        s_prepared = false;
        s_status.active = false;
        set_status_message("SD busy");
        return false;
    }
    if (!setup_usb_msc())
    {
        s_storage_session.end();
        restore_mass_storage_mode();
        s_prepared = false;
        s_status.active = false;
        return false;
    }

    platform::ui::device::delay_ms(500);
    s_status.active = true;
    if (s_message[0] == '\0')
    {
        set_status_message("USB Started - Ready");
    }
    return true;
#else
    return false;
#endif
}

void stop()
{
#if defined(ARDUINO_USB_MODE)
    if (s_backend_started)
    {
        s_msc.end();
        s_backend_started = false;
    }
    s_storage_session.end();
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
    s_status.message = s_message;
    return s_status;
}

} // namespace platform::ui::usb_support
