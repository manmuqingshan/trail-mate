#include "platform/ui/gps_runtime.h"

#include <cmath>

#include "platform/esp/idf_common/gps_runtime.h"

namespace platform::ui::gps
{

GpsState get_data()
{
    return ::platform::esp::idf_common::gps_runtime::get_data();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    return ::platform::esp::idf_common::gps_runtime::get_gnss_snapshot(out, max, out_count, status);
}

GpsDiagnosticsSnapshot diagnostics()
{
    return ::platform::esp::idf_common::gps_runtime::diagnostics();
}

uint32_t last_motion_ms()
{
    return ::platform::esp::idf_common::gps_runtime::last_motion_ms();
}

void tick_service()
{
    // ESP GPS is driven by its own runtime task; UI/services consume snapshots.
}

bool supports_receiver_baud_setting()
{
    return true;
}

bool supports_receiver_init_policy_settings()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // P4 currently applies the selected UART baud on the next GNSS lifecycle,
    // but does not send receiver profile/policy commands.  Do not show a
    // settings group whose values cannot reach the GNSS receiver.
    return false;
#else
    return true;
#endif
}

bool supports_gnss_runtime_settings()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // The P4 runtime parses the receiver's current NMEA stream; it does not
    // yet program constellation mode or satellite mask commands.
    return false;
#else
    return true;
#endif
}

bool supports_collection_interval_setting()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // There is no P4 receiver command or sampling scheduler behind this UI
    // setting, so exposing it makes the configuration misleading.
    return false;
#else
    return true;
#endif
}

bool supports_external_nmea_output_setting()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // P4 has no external NMEA output transport wired to this setting.
    return false;
#else
    return true;
#endif
}

bool supports_altitude_reference_setting()
{
    return true;
}

bool supports_coordinate_format_setting()
{
    return true;
}

bool is_enabled()
{
    return ::platform::esp::idf_common::gps_runtime::is_enabled();
}

bool is_powered()
{
    return ::platform::esp::idf_common::gps_runtime::is_powered();
}

void set_enabled(bool enabled)
{
    ::platform::esp::idf_common::gps_runtime::set_enabled(enabled);
}

void set_collection_interval(uint32_t interval_ms)
{
    ::platform::esp::idf_common::gps_runtime::set_collection_interval(interval_ms);
}

void set_power_strategy(uint8_t strategy)
{
    ::platform::esp::idf_common::gps_runtime::set_power_strategy(strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    ::platform::esp::idf_common::gps_runtime::set_gnss_config(mode, sat_mask);
}

void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    ::platform::esp::idf_common::gps_runtime::set_external_nmea_config(output_hz, sentence_mask);
}

void set_receiver_init_config(const GpsReceiverInitConfig& config)
{
    ::platform::esp::idf_common::gps_runtime::set_receiver_init_config(config);
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    ::platform::esp::idf_common::gps_runtime::set_motion_idle_timeout(timeout_ms);
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    ::platform::esp::idf_common::gps_runtime::set_motion_sensor_id(sensor_id);
}

void acquire_power_lease(const char* reason)
{
    (void)reason;
}

void release_power_lease(const char* reason)
{
    (void)reason;
}

void suspend_runtime()
{
    TaskHandle_t task_handle = ::platform::esp::idf_common::gps_runtime::get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskSuspend(task_handle);
    }
}

void resume_runtime()
{
    TaskHandle_t task_handle = ::platform::esp::idf_common::gps_runtime::get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskResume(task_handle);
    }
}

double calculate_map_resolution(int zoom, double lat)
{
    constexpr double kMaxLatitude = 85.05112878;
    double lat_clamped = lat;
    if (lat_clamped > kMaxLatitude)
    {
        lat_clamped = kMaxLatitude;
    }
    if (lat_clamped < -kMaxLatitude)
    {
        lat_clamped = -kMaxLatitude;
    }

    const double resolution_equator = 156543.03392 / std::pow(2.0, zoom);
    const double lat_rad = lat_clamped * 3.14159265358979323846 / 180.0;
    return resolution_equator * std::cos(lat_rad);
}

} // namespace platform::ui::gps
