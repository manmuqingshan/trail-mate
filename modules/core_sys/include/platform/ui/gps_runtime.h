#pragma once

#include <cstddef>
#include <cstdint>

#include "gps/domain/gnss_satellite.h"
#include "gps/domain/gps_diagnostics.h"
#include "gps/domain/gps_state.h"
#include "gps/usecase/gps_runtime_config.h"

namespace platform::ui::gps
{

using GpsState = ::gps::GpsState;
using GnssSatInfo = ::gps::GnssSatInfo;
using GnssStatus = ::gps::GnssStatus;
using GnssFix = ::gps::GnssFix;
using GnssSystem = ::gps::GnssSystem;
using GpsDiagnosticsSnapshot = ::gps::GpsDiagnosticsSnapshot;
using GpsReceiverInitConfig = ::gps::GpsReceiverInitConfig;

enum class FallbackMode : uint8_t
{
    LiveOnly,
    DemoDefaults,
};

GpsState get_data();
bool get_gnss_snapshot(GnssSatInfo* out, std::size_t max, std::size_t* out_count, GnssStatus* status);
GpsDiagnosticsSnapshot diagnostics();
uint32_t last_motion_ms();
void tick_service();
bool supports_receiver_baud_setting();
bool supports_receiver_init_policy_settings();
bool supports_gnss_runtime_settings();
bool supports_collection_interval_setting();
bool supports_external_nmea_output_setting();
bool supports_altitude_reference_setting();
bool supports_coordinate_format_setting();
bool is_enabled();
bool is_powered();
void set_enabled(bool enabled);
void set_collection_interval(uint32_t interval_ms);
void set_power_strategy(uint8_t strategy);
void set_gnss_config(uint8_t mode, uint8_t sat_mask);
void set_external_nmea_config(uint8_t output_hz, uint8_t sentence_mask);
void set_receiver_init_config(const GpsReceiverInitConfig& config);
void set_fallback_mode(FallbackMode mode);
void set_motion_idle_timeout(uint32_t timeout_ms);
void set_motion_sensor_id(uint8_t sensor_id);
void acquire_power_lease(const char* reason);
void release_power_lease(const char* reason);
void suspend_runtime();
void resume_runtime();
double calculate_map_resolution(int zoom, double lat);

} // namespace platform::ui::gps
