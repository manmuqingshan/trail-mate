#include "platform/ui/usb_support_runtime.h"

#include "app/app_facade_access.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/gps/gps_service_api.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "screen_sleep.h"
#include "team/usecase/team_pairing_service.h"

#include <cstdio>

#if defined(ARDUINO_USB_MODE)
#include <USB.h>
#include <USBMSC.h>
#include <esp_attr.h>
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
bool s_radio_hardware_quiesced_by_usb = false;
TaskHandle_t s_gps_task_suspended_by_usb = nullptr;
::platform::ui::wifi::ExternalStorageSuspension s_wifi_suspension{};

#define USB_MSC_LOG(...) std::printf("[USBMSC] " __VA_ARGS__)

void set_status_message(const char* message)
{
    const char* source = (message && message[0] != '\0') ? message : "";
    std::snprintf(s_message, sizeof(s_message), "%s", source);
    s_status.message = s_message;
}

void stop_pairing()
{
    if (!app::hasAppFacade())
    {
        return;
    }
    if (team::TeamPairingService* pairing = app::teamFacade().getTeamPairing())
    {
        pairing->stop();
    }
}

#if defined(ARDUINO_USB_MODE)
USBMSC s_msc;
bool s_backend_started = false;

class UsbMscStorageSession final
{
  public:
    bool begin()
    {
        if (active_)
        {
            return true;
        }

        // The transition takes the same runtime lock as every SD operation,
        // so no application access can cross the point at which USB MSC
        // becomes the card's sole logical owner. Each raw sector operation
        // below still obtains its own physical shared-SPI transaction through
        // the SdFat driver hook.
        if (!::platform::esp::arduino_common::storage::sd_set_external_block_owner_active(
                true))
        {
            return false;
        }
        active_ = true;
        return true;
    }

    bool end()
    {
        if (!active_)
        {
            return true;
        }
        if (!::platform::esp::arduino_common::storage::sd_set_external_block_owner_active(
                false))
        {
            return false;
        }
        active_ = false;
        return true;
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

enum class ExitTracePhase : uint8_t
{
    None = 0,
    StopRequested,
    MscCallbacksEnded,
    SdOwnershipReleased,
    RestartRequested,
};

constexpr uint32_t k_exit_trace_magic = 0x554D5343U; // "UMSC"
constexpr uint32_t k_exit_trace_msc_ended = 1U << 0;
constexpr uint32_t k_exit_trace_sd_released = 1U << 1;
constexpr uint32_t k_exit_trace_restart_requested = 1U << 2;

struct UsbMscExitTrace
{
    uint32_t magic;
    uint32_t session;
    uint32_t flags;
    uint8_t phase;
    uint8_t sd_release_succeeded;
    uint16_t reserved;
};

// RTC_NOINIT_ATTR is deliberately uninitialized: the ESP restart path must
// preserve this tiny record until the next normal boot can report it.
RTC_NOINIT_ATTR volatile UsbMscExitTrace s_exit_trace;

const char* exit_trace_phase_name(uint8_t phase)
{
    switch (static_cast<ExitTracePhase>(phase))
    {
    case ExitTracePhase::None:
        return "none";
    case ExitTracePhase::StopRequested:
        return "stop_requested";
    case ExitTracePhase::MscCallbacksEnded:
        return "msc_callbacks_ended";
    case ExitTracePhase::SdOwnershipReleased:
        return "sd_ownership_released";
    case ExitTracePhase::RestartRequested:
        return "restart_requested";
    }
    return "unknown";
}

void begin_exit_trace()
{
    const uint32_t previous_session =
        s_exit_trace.magic == k_exit_trace_magic ? s_exit_trace.session : 0U;
    s_exit_trace.magic = k_exit_trace_magic;
    s_exit_trace.session = previous_session + 1U;
    s_exit_trace.flags = 0U;
    s_exit_trace.phase = static_cast<uint8_t>(ExitTracePhase::StopRequested);
    s_exit_trace.sd_release_succeeded = 0U;
    s_exit_trace.reserved = 0U;
}

void mark_exit_trace(ExitTracePhase phase, uint32_t flag)
{
    s_exit_trace.phase = static_cast<uint8_t>(phase);
    s_exit_trace.flags |= flag;
}

int32_t usbReadCallback(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
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

bool prepare_mass_storage_mode()
{
    if (!::platform::ui::wifi::suspend_for_external_storage(&s_wifi_suspension))
    {
        USB_MSC_LOG("Wi-Fi quiesce failed for USB mass storage\n");
        set_status_message("Wi-Fi Busy");
        return false;
    }

    if (app::AppTasks::areRadioTasksPaused())
    {
        USB_MSC_LOG("radio already owned by another exclusive session\n");
        ::platform::ui::wifi::resume_after_external_storage(&s_wifi_suspension);
        set_status_message("Radio Busy");
        return false;
    }

    if (app::AppTasks::pauseRadioTasks())
    {
        s_radio_tasks_paused_by_usb = true;
        USB_MSC_LOG("radio tasks paused for USB mass storage\n");
    }
    else
    {
        USB_MSC_LOG("radio task quiesce failed for USB mass storage\n");
        ::platform::ui::wifi::resume_after_external_storage(&s_wifi_suspension);
        set_status_message("Radio Busy");
        return false;
    }

    if (!app::AppTasks::quiesceRadioHardwareForExternalStorage())
    {
        USB_MSC_LOG("radio hardware standby failed for USB mass storage\n");
        app::AppTasks::resumeRadioTasks();
        s_radio_tasks_paused_by_usb = false;
        ::platform::ui::wifi::resume_after_external_storage(&s_wifi_suspension);
        set_status_message("Radio Busy");
        return false;
    }
    s_radio_hardware_quiesced_by_usb = true;

    TaskHandle_t gps_task_handle = gps::gps_get_task_handle();
    if (gps_task_handle != nullptr && eTaskGetState(gps_task_handle) != eSuspended)
    {
        vTaskSuspend(gps_task_handle);
        s_gps_task_suspended_by_usb = gps_task_handle;
    }

    stop_pairing();
    ::platform::ui::screen::disable_sleep();
    return true;
}

void restore_mass_storage_mode()
{
    ::platform::ui::screen::enable_sleep();

    if (s_gps_task_suspended_by_usb != nullptr)
    {
        vTaskResume(s_gps_task_suspended_by_usb);
        s_gps_task_suspended_by_usb = nullptr;
    }

    if (s_radio_hardware_quiesced_by_usb)
    {
        if (s_radio_tasks_paused_by_usb)
        {
            app::AppTasks::resumeRadioTasks();
            s_radio_tasks_paused_by_usb = false;
            USB_MSC_LOG("radio tasks resumed after USB mass storage\n");
        }
        s_radio_hardware_quiesced_by_usb = false;
    }

    ::platform::ui::wifi::resume_after_external_storage(&s_wifi_suspension);
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

    if (!prepare_mass_storage_mode())
    {
        s_status.active = false;
        return false;
    }
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
        // Arduino-ESP32 exposes no TinyUSB teardown. End MSC, release the SD
        // lease, then let a normal restart rebuild every application service.
        begin_exit_trace();
        s_msc.end();
        mark_exit_trace(ExitTracePhase::MscCallbacksEnded, k_exit_trace_msc_ended);
        s_exit_trace.sd_release_succeeded = s_storage_session.end() ? 1U : 0U;
        if (s_exit_trace.sd_release_succeeded != 0U)
        {
            mark_exit_trace(ExitTracePhase::SdOwnershipReleased,
                            k_exit_trace_sd_released);
        }
        s_backend_started = false;
        mark_exit_trace(ExitTracePhase::RestartRequested,
                        k_exit_trace_restart_requested);
        platform::ui::device::restart();
        return;
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

void report_previous_exit_trace()
{
#if defined(ARDUINO_USB_MODE)
    if (s_exit_trace.magic != k_exit_trace_magic)
    {
        return;
    }

    USB_MSC_LOG(
        "previous exit trace session=%lu phase=%s(%u) flags=0x%08lx sd_release=%u\n",
        static_cast<unsigned long>(s_exit_trace.session),
        exit_trace_phase_name(s_exit_trace.phase),
        static_cast<unsigned>(s_exit_trace.phase),
        static_cast<unsigned long>(s_exit_trace.flags),
        static_cast<unsigned>(s_exit_trace.sd_release_succeeded));
    USB_MSC_LOG("previous exit trace stages: msc_end=%u sd_owner=%u restart=%u\n",
                (s_exit_trace.flags & k_exit_trace_msc_ended) != 0U ? 1U : 0U,
                (s_exit_trace.flags & k_exit_trace_sd_released) != 0U ? 1U : 0U,
                (s_exit_trace.flags & k_exit_trace_restart_requested) != 0U ? 1U : 0U);
    s_exit_trace.magic = 0U;
#endif
}

Status get_status()
{
    s_status.message = s_message;
    return s_status;
}

} // namespace platform::ui::usb_support
