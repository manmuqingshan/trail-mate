#include "platform/ui/gps_runtime.h"

#include "platform/esp/arduino_common/gps/gps_service_api.h"

namespace platform::ui::gps
{

GpsState get_data()
{
    return ::gps::gps_get_data();
}

bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status)
{
    return ::gps::gps_get_gnss_snapshot(out, max, out_count, status);
}

GpsDiagnosticsSnapshot diagnostics()
{
    return ::gps::gps_get_diagnostics();
}

uint32_t last_motion_ms()
{
    return ::gps::gps_get_last_motion_ms();
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
    return true;
}

bool supports_gnss_runtime_settings()
{
    return true;
}

bool supports_collection_interval_setting()
{
    return true;
}

bool supports_external_nmea_output_setting()
{
    return true;
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
    return ::gps::gps_is_enabled();
}

bool is_powered()
{
    return ::gps::gps_is_powered();
}

void set_enabled(bool enabled)
{
    ::gps::gps_set_enabled(enabled);
}

void set_collection_interval(uint32_t interval_ms)
{
    ::gps::gps_set_collection_interval(interval_ms);
}

void set_power_strategy(uint8_t strategy)
{
    ::gps::gps_set_power_strategy(strategy);
}

void set_gnss_config(uint8_t mode, uint8_t sat_mask)
{
    ::gps::gps_set_gnss_config(mode, sat_mask);
}

void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask)
{
    ::gps::gps_set_external_nmea_config(output_hz, sentence_mask);
}

void set_receiver_init_config(const GpsReceiverInitConfig& config)
{
    ::gps::gps_set_receiver_init_config(config);
}

void set_motion_idle_timeout(uint32_t timeout_ms)
{
    ::gps::gps_set_motion_idle_timeout(timeout_ms);
}

void set_motion_sensor_id(uint8_t sensor_id)
{
    ::gps::gps_set_motion_sensor_id(sensor_id);
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
    TaskHandle_t task_handle = ::gps::gps_get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskSuspend(task_handle);
    }
}

void resume_runtime()
{
    TaskHandle_t task_handle = ::gps::gps_get_task_handle();
    if (task_handle != nullptr)
    {
        vTaskResume(task_handle);
    }
}

double calculate_map_resolution(int zoom, double lat)
{
    return ::gps::calculate_map_resolution(zoom, lat);
}

} // namespace platform::ui::gps
