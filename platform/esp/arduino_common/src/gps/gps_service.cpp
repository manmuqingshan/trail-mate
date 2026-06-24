#include "platform/esp/arduino_common/gps/gps_service.h"

#include "board/GpsBoard.h"
#include "board/TLoRaPagerTypes.h"
#include "gps/usecase/gps_runtime_policy.h"
#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/power_tier.h"
#include <cstdio>
#include <cstring>

// GPS task loop diagnostics are high frequency and can stall touch/LVGL work on ESP.
// Enable only while actively debugging receiver bring-up.
#ifndef TRAIL_MATE_GPS_TASK_DEBUG
#define TRAIL_MATE_GPS_TASK_DEBUG 0
#endif

#if TRAIL_MATE_GPS_TASK_DEBUG
#define GPS_TASK_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define GPS_TASK_LOG(...) \
    do                    \
    {                     \
    } while (0)
#endif

namespace
{

constexpr uint32_t kGpsUartPollIntervalMs = 250;
constexpr uint32_t kGpsIdlePollIntervalMs = 1000;
constexpr uint32_t kGpsHealthLogIntervalMs = 10000;
constexpr uint32_t kGpsPostConfigWatchMs = 8000;

const char* gps_valid_text(bool valid)
{
    return valid ? "fix" : "nofix";
}

} // namespace

namespace gps
{

GpsService& GpsService::getInstance()
{
    static GpsService instance;
    return instance;
}

void GpsService::begin(GpsBoard& gps_board, MotionBoard& motion_board,
                       uint32_t disable_hw_init, uint32_t gps_interval_ms,
                       const MotionConfig& motion_config,
                       const GpsReceiverInitConfig& receiver_init_config)
{
    gps_board_ = &gps_board;
    motion_board_ = &motion_board;
    receiver_init_config_ = receiver_init_config;
    gps_board_->setGPSReceiverInitConfig(receiver_init_config_);
    gps_adapter_.begin(gps_board);

    gps_disabled_ = (disable_hw_init & NO_HW_GPS) != 0;
    if (gps_disabled_)
    {
        gps_ready_ = false;
        user_enabled_ = false;
        return;
    }
    gps_ready_ = gps_adapter_.isReady();
    gps_powered_ = gps_ready_;
    if (!gps_ready_)
    {
        Serial.printf("[GPS] service starting reason=adapter_not_ready_waiting_retry\n");
    }

    motion_adapter_.begin(motion_board);

    gps_data_mutex_ = xSemaphoreCreateMutex();
    if (gps_data_mutex_ == NULL)
    {
        log_e("Failed to create GPS data mutex");
    }
    gps_init_mutex_ = xSemaphoreCreateRecursiveMutex();
    if (gps_init_mutex_ == NULL)
    {
        log_e("Failed to create GPS init mutex");
    }

    runtime_state_.setCollectionInterval(gps_interval_ms);
    motion_config_ = normalizeMotionConfig(motion_config);

    BaseType_t task_result = xTaskCreate(
        gpsTask,
        "gps_collect",
        4 * 1024,
        this,
        5,
        &gps_task_handle_);
    if (task_result != pdPASS)
    {
        log_e("Failed to create GPS data collection task");
    }
    else
    {
        log_d("GPS data collection task created successfully (interval: %lu ms)", runtime_state_.requestedCollectionIntervalMs());
    }

    const bool motion_control_enabled = motion_policy_.begin(motion_adapter_, motion_config_);
    runtime_state_.setMotionControlEnabled(motion_control_enabled, millis());

    if (motion_control_enabled && motion_task_handle_ == nullptr)
    {
        task_result = xTaskCreate(
            motionTask,
            "motion_mgr",
            3 * 1024,
            this,
            6,
            &motion_task_handle_);
        if (task_result != pdPASS)
        {
            log_e("Failed to create motion manager task");
        }
        else
        {
            log_d("Motion manager task created successfully");
        }
    }
}

GpsState GpsService::getData()
{
    GpsState data{};
    if (gps_data_mutex_ != NULL)
    {
        if (xSemaphoreTake(gps_data_mutex_, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            data = gps_state_;
            if (data.valid && gps_last_update_time_ > 0)
            {
                data.age = millis() - gps_last_update_time_;
            }
            else
            {
                data.age = UINT32_MAX;
            }
            xSemaphoreGive(gps_data_mutex_);
        }
    }
    return data;
}

bool GpsService::getGnssSnapshot(GnssSatInfo* out, size_t max, size_t* out_count, GnssStatus* status)
{
    if (!out || max == 0)
    {
        if (out_count)
        {
            *out_count = 0;
        }
        if (status)
        {
            *status = GnssStatus{};
        }
        return false;
    }

    if (gps_data_mutex_ != NULL)
    {
        if (xSemaphoreTake(gps_data_mutex_, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            size_t copy_count = gnss_sat_count_;
            if (copy_count > max)
            {
                copy_count = max;
            }
            if (copy_count > 0)
            {
                memcpy(out, gnss_sats_, sizeof(GnssSatInfo) * copy_count);
            }
            if (out_count)
            {
                *out_count = copy_count;
            }
            if (status)
            {
                *status = gnss_status_;
            }
            xSemaphoreGive(gps_data_mutex_);
            return true;
        }
    }
    return false;
}

GpsDiagnosticsSnapshot GpsService::getDiagnostics()
{
    GpsDiagnosticsSnapshot snapshot{};
    snapshot.supported = !gps_disabled_;
    snapshot.enabled = isEnabled();
    snapshot.powered = gps_powered_;
    snapshot.ready = gps_ready_;
    snapshot.chars_total = gps_chars_total_;
    snapshot.chars_recent = gps_chars_recent_;
    snapshot.poll_interval_ms = kGpsUartPollIntervalMs;
    snapshot.collection_interval_ms = getCollectionInterval();
    snapshot.last_rx_age_ms = gps_last_rx_ms_ > 0 ? (millis() - gps_last_rx_ms_) : 0xFFFFFFFFUL;

    if (gps_data_mutex_ != NULL && xSemaphoreTake(gps_data_mutex_, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        snapshot.has_fix = gps_state_.valid;
        snapshot.satellites = gps_state_.satellites;
        snapshot.sats_in_view = gnss_status_.sats_in_view;
        snapshot.sats_in_use = gnss_status_.sats_in_use;
        xSemaphoreGive(gps_data_mutex_);
    }

    if (!snapshot.supported)
    {
        snapshot.code = GpsDiagnosticCode::Disabled;
    }
    else if (!snapshot.enabled)
    {
        snapshot.code = GpsDiagnosticCode::NotEnabled;
    }
    else if (!snapshot.powered)
    {
        snapshot.code = GpsDiagnosticCode::PowerOff;
    }
    else if (!snapshot.ready)
    {
        snapshot.code = GpsDiagnosticCode::TransportNotReady;
    }
    else if (snapshot.chars_total == 0)
    {
        snapshot.code = GpsDiagnosticCode::NoTraffic;
    }
    else if (snapshot.last_rx_age_ms != 0xFFFFFFFFUL && snapshot.last_rx_age_ms > 15000UL)
    {
        snapshot.code = GpsDiagnosticCode::TrafficStalled;
    }
    else if (!snapshot.has_fix)
    {
        snapshot.code = GpsDiagnosticCode::NoFix;
    }
    else
    {
        snapshot.code = GpsDiagnosticCode::OK;
    }

    return snapshot;
}

uint32_t GpsService::getCollectionInterval() const
{
    return runtime_state_.collectionIntervalMs(getPowerTier());
}

uint32_t GpsService::getLastMotionMs() const
{
    return motion_policy_.lastMotionMs();
}

void GpsService::setEnabled(bool enabled)
{
    if (gps_disabled_)
    {
        user_enabled_ = false;
        return;
    }

    user_enabled_ = enabled;
    if (!enabled)
    {
        setGPSPowerState(false);
        if (gps_data_mutex_ != NULL && xSemaphoreTake(gps_data_mutex_, portMAX_DELAY) == pdTRUE)
        {
            gps_state_ = GpsState{};
            gnss_sat_count_ = 0;
            gnss_status_ = GnssStatus{};
            gps_last_update_time_ = 0;
            xSemaphoreGive(gps_data_mutex_);
        }
        return;
    }

    updateMotionState(millis());
}

void GpsService::setCollectionInterval(uint32_t interval_ms)
{
    interval_ms = normalizeCollectionInterval(interval_ms);

    if (gps_data_mutex_ != NULL)
    {
        if (xSemaphoreTake(gps_data_mutex_, portMAX_DELAY) == pdTRUE)
        {
            runtime_state_.setCollectionInterval(interval_ms);
            xSemaphoreGive(gps_data_mutex_);
        }
    }
}

void GpsService::setPowerStrategy(uint8_t strategy)
{
    runtime_state_.setPowerStrategy(strategy);

    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }

    if ((strategy == 0 || strategy == 1) && !runtime_state_.motionControlEnabled())
    {
        setMotionConfig(motion_config_);
        return;
    }

    updateMotionState(millis());
}

void GpsService::setTeamModeActive(bool active)
{
    if (!runtime_state_.setTeamModeActive(active))
    {
        return;
    }

    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }

    updateMotionState(millis());
}

void GpsService::setGnssConfig(uint8_t mode, uint8_t sat_mask)
{
    runtime_config_.setGnssConfig(mode, sat_mask);

    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }

    if (gps_powered_)
    {
        applyGnssConfig();
    }
}

void GpsService::setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    runtime_config_.setExternalNmeaConfig(output_hz, sentence_mask);
    runtime_config_.markExternalNmeaConfigApplied();
}

void GpsService::setReceiverInitConfig(const GpsReceiverInitConfig& config)
{
    if (!takeGpsUartLock())
    {
        return;
    }
    receiver_init_config_ = config;
    if (gps_board_ != nullptr)
    {
        gps_board_->setGPSReceiverInitConfig(receiver_init_config_);
    }
    giveGpsUartLock();
}

void GpsService::setMotionConfig(const MotionConfig& config)
{
    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }

    motion_config_ = normalizeMotionConfig(config);

    const bool motion_control_enabled = motion_policy_.begin(motion_adapter_, motion_config_);
    runtime_state_.setMotionControlEnabled(motion_control_enabled, millis());

    if (motion_control_enabled && motion_task_handle_ == nullptr)
    {
        BaseType_t task_result = xTaskCreate(
            motionTask,
            "motion_mgr",
            3 * 1024,
            this,
            6,
            &motion_task_handle_);
        if (task_result != pdPASS)
        {
            log_e("Failed to create motion manager task");
        }
        else
        {
            log_d("Motion manager task created successfully");
        }
    }

    updateMotionState(millis());
}

void GpsService::applyGnssConfig()
{
    if (!runtime_config_.hasPendingGnssConfig())
    {
        return;
    }
    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }
    if (!takeGpsUartLock())
    {
        return;
    }
    const GnssRuntimeConfig& gnss_config = runtime_config_.gnssConfig();
    bool send_rxm = receiver_init_config_.rxm_policy != 1;
    bool send_gnss = receiver_init_config_.gnss_policy != 1;
#if defined(ARDUINO_T_DECK_PRO)
    const bool tdeck_legacy_or_unknown = receiver_init_config_.profile == 0 ||
                                         receiver_init_config_.profile == 1 ||
                                         receiver_init_config_.profile == 2;
    if (tdeck_legacy_or_unknown && receiver_init_config_.rxm_policy == 0)
    {
        send_rxm = false;
    }
    if (tdeck_legacy_or_unknown && receiver_init_config_.gnss_policy == 0)
    {
        send_gnss = false;
    }
#elif defined(ARDUINO_T_DECK)
    if ((send_rxm || send_gnss) && !canSendReceiverUbxConfig("applyGnssConfig"))
    {
        send_rxm = false;
        send_gnss = false;
    }
#endif
    if (!send_rxm && !send_gnss)
    {
        Serial.printf("[GPS] applyGnssConfig skipped rxm_policy=%u gnss_policy=%u profile=%u mode=%u sat_mask=0x%02X\n",
                      static_cast<unsigned>(receiver_init_config_.rxm_policy),
                      static_cast<unsigned>(receiver_init_config_.gnss_policy),
                      static_cast<unsigned>(receiver_init_config_.profile),
                      static_cast<unsigned>(gnss_config.mode),
                      static_cast<unsigned>(gnss_config.sat_mask));
        runtime_config_.markGnssConfigApplied();
        giveGpsUartLock();
        return;
    }
    const uint32_t chars_before = gps_chars_total_;
    const uint32_t started_ms = millis();
    const bool ok = gps_adapter_.applyGnssConfig(gnss_config.mode, gnss_config.sat_mask, send_rxm, send_gnss);
    const uint32_t elapsed_ms = millis() - started_ms;
    Serial.printf("[GPS] applyGnssConfig mode=%u sat_mask=0x%02X ok=%d powered=%d ready=%d chars_before=%lu chars_after=%lu elapsed_ms=%lu rxm_send=%d gnss_send=%d profile=%u\n",
                  static_cast<unsigned>(gnss_config.mode),
                  static_cast<unsigned>(gnss_config.sat_mask),
                  ok ? 1 : 0,
                  gps_powered_ ? 1 : 0,
                  gps_ready_ ? 1 : 0,
                  static_cast<unsigned long>(chars_before),
                  static_cast<unsigned long>(gps_chars_total_),
                  static_cast<unsigned long>(elapsed_ms),
                  send_rxm ? 1 : 0,
                  send_gnss ? 1 : 0,
                  static_cast<unsigned>(receiver_init_config_.profile));
    startPostConfigStreamWatch("applyGnssConfig", gps_chars_total_);
    if (!ok)
    {
        giveGpsUartLock();
        return;
    }
    runtime_config_.markGnssConfigApplied();
    giveGpsUartLock();
}

void GpsService::applyInternalNmeaConfig()
{
    if (!isEnabled() || gps_board_ == nullptr)
    {
        return;
    }
    if (!takeGpsUartLock())
    {
        return;
    }
    if (receiver_init_config_.nmea_policy == 1)
    {
        Serial.printf("[GPS] applyInternalNmeaConfig skipped policy=skip\n");
        giveGpsUartLock();
        return;
    }
#if defined(ARDUINO_T_DECK_PRO)
    if (receiver_init_config_.nmea_policy == 0 &&
        (receiver_init_config_.profile == 0 || receiver_init_config_.profile == 1 || receiver_init_config_.profile == 2))
    {
        Serial.printf("[GPS] applyInternalNmeaConfig skipped policy=auto_legacy profile=%u\n",
                      static_cast<unsigned>(receiver_init_config_.profile));
        giveGpsUartLock();
        return;
    }
#elif defined(ARDUINO_T_DECK)
    if (!canSendReceiverUbxConfig("applyInternalNmeaConfig"))
    {
        giveGpsUartLock();
        return;
    }
#endif
    const uint32_t chars_before = gps_chars_total_;
    const uint32_t started_ms = millis();
    const bool ok = gps_adapter_.applyNmeaConfig(1, 0);
    const uint32_t elapsed_ms = millis() - started_ms;
    Serial.printf("[GPS] applyInternalNmeaConfig rate=1 sentence_mask=0 ok=%d powered=%d ready=%d chars_before=%lu chars_after=%lu elapsed_ms=%lu ubx=cfg_msg\n",
                  ok ? 1 : 0,
                  gps_powered_ ? 1 : 0,
                  gps_ready_ ? 1 : 0,
                  static_cast<unsigned long>(chars_before),
                  static_cast<unsigned long>(gps_chars_total_),
                  static_cast<unsigned long>(elapsed_ms));
    startPostConfigStreamWatch("applyInternalNmeaConfig", gps_chars_total_);
    if (!ok)
    {
        giveGpsUartLock();
        return;
    }
    giveGpsUartLock();
}

bool GpsService::takeGpsUartLock(TickType_t timeout)
{
    return gps_init_mutex_ == nullptr || xSemaphoreTakeRecursive(gps_init_mutex_, timeout) == pdTRUE;
}

void GpsService::giveGpsUartLock()
{
    if (gps_init_mutex_ != nullptr)
    {
        xSemaphoreGiveRecursive(gps_init_mutex_);
    }
}

bool GpsService::canSendReceiverUbxConfig(const char* source) const
{
#if defined(ARDUINO_T_DECK)
    if (gps_board_ == nullptr)
    {
        return false;
    }

    const GpsReceiverProtocol protocol = gps_board_->getGPSReceiverProtocol();
    const bool explicit_ubx_profile = receiver_init_config_.profile == 2 ||
                                      receiver_init_config_.profile == 3;
    if (protocol == GpsReceiverProtocol::Ubx)
    {
        return true;
    }
    if (explicit_ubx_profile)
    {
        Serial.printf("[GPS] %s ubx_config allowed explicit_profile=%u detected_protocol=%s\n",
                      source ? source : "gps_config",
                      static_cast<unsigned>(receiver_init_config_.profile),
                      gpsReceiverProtocolName(protocol));
        return true;
    }

    Serial.printf("[GPS] %s skipped ubx_config profile=%u detected_protocol=%s chars_total=%lu\n",
                  source ? source : "gps_config",
                  static_cast<unsigned>(receiver_init_config_.profile),
                  gpsReceiverProtocolName(protocol),
                  static_cast<unsigned long>(gps_chars_total_));
    return false;
#else
    (void)source;
    return true;
#endif
}

void GpsService::startPostConfigStreamWatch(const char* label, uint32_t chars_total)
{
    gps_post_config_watch_active_ = true;
    gps_post_config_start_ms_ = millis();
    gps_post_config_chars_ = chars_total;
    std::snprintf(gps_post_config_label_,
                  sizeof(gps_post_config_label_),
                  "%s",
                  label ? label : "gps_config");
    Serial.printf("[GPS] post_config watch source=%s chars=%lu window_ms=%lu\n",
                  gps_post_config_label_,
                  static_cast<unsigned long>(gps_post_config_chars_),
                  static_cast<unsigned long>(kGpsPostConfigWatchMs));
}

void GpsService::setMotionIdleTimeout(uint32_t timeout_ms)
{
    MotionConfig cfg = motion_config_;
    cfg.idle_timeout_ms = timeout_ms;
    setMotionConfig(cfg);
}

void GpsService::setMotionSensorId(uint8_t sensor_id)
{
    MotionConfig cfg = motion_config_;
    cfg.sensor_id = sensor_id;
    setMotionConfig(cfg);
}

void GpsService::gpsTask(void* pvParameters)
{
    GpsService* service = static_cast<GpsService*>(pvParameters);
    if (service == nullptr)
    {
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t loop_count = 0;
    uint32_t task_start_ms = millis();
    uint32_t last_log_ms = 0;
    uint32_t last_health_log_ms = 0;
    uint32_t last_health_total_chars = 0;
    uint32_t last_health_total_uart_reads = 0;
    uint32_t last_total_chars = 0;
    uint32_t total_uart_reads = 0;

    GPS_TASK_LOG("[GPS Task] ===== TASK STARTED =====\n");
    GPS_TASK_LOG("[GPS Task] Started at %lu ms, GPS ready: %d\n", task_start_ms, service->gps_adapter_.isReady());
    GPS_TASK_LOG("[GPS Task] Collection interval: %lu ms\n", service->getCollectionInterval());
    Serial.printf("[GPS] task started ready=%d powered=%d poll_ms=%lu collection_ms=%lu\n",
                  service->gps_adapter_.isReady() ? 1 : 0,
                  service->gps_powered_ ? 1 : 0,
                  static_cast<unsigned long>(kGpsUartPollIntervalMs),
                  static_cast<unsigned long>(service->getCollectionInterval()));

    while (true)
    {
        loop_count++;
        uint32_t now_ms = millis();
        bool gps_ready = service->gps_adapter_.isReady();
        service->gps_ready_ = gps_ready;
        uint32_t total_chars = last_total_chars;
        uint32_t chars_this_loop = 0;
        uint32_t uart_read_bytes = 0;
        bool health_valid = false;
        uint8_t health_sat_count = 0;
        uint8_t health_sats_in_view = 0;
        uint8_t health_sats_in_use = 0;
        double health_lat = 0.0;
        double health_lng = 0.0;
        uint32_t health_fix_age_ms = UINT32_MAX;

        if (service->motion_adapter_.isReady() && service->motion_policy_.isEnabled())
        {
            if (service->motion_policy_.shouldUpdateSensor(now_ms))
            {
                service->motion_adapter_.update();
                service->motion_policy_.markSensorUpdated(now_ms);
            }
        }

        bool should_log = (loop_count <= 10) ||
                          (loop_count % 10 == 0) ||
                          ((now_ms - last_log_ms) >= 5000);

        if (should_log)
        {
            GPS_TASK_LOG("[GPS Task] Loop %lu: GPS ready=%d, valid=%d, mutex_ok=%d\n",
                         loop_count,
                         gps_ready,
                         service->gps_state_.valid,
                         service->gps_data_mutex_ != NULL ? 1 : 0);
            last_log_ms = now_ms;
        }

        if (!service->gps_powered_)
        {
            if (should_log)
            {
                GPS_TASK_LOG("[GPS Task] GPS power OFF (motion_control=%d), skipping (loop %lu)\n",
                             service->runtime_state_.motionControlEnabled() ? 1 : 0,
                             loop_count);
            }
        }
        else if (service->gps_initializing_)
        {
            if (should_log)
            {
                GPS_TASK_LOG("[GPS Task] GPS initialization in progress, waiting (loop %lu)\n", loop_count);
            }
        }
        else if (gps_ready)
        {
            if (service->takeGpsUartLock(pdMS_TO_TICKS(20)))
            {
                total_chars = service->gps_adapter_.loop();
                uart_read_bytes = service->gps_adapter_.lastLoopReadBytes();
                total_uart_reads += uart_read_bytes;
                chars_this_loop = (total_chars > last_total_chars) ? (total_chars - last_total_chars) : 0;
                last_total_chars = total_chars;
                service->gps_chars_total_ = total_chars;
                if (uart_read_bytes > 0)
                {
                    service->gps_last_rx_ms_ = now_ms;
                }
                service->giveGpsUartLock();
            }
            else
            {
                total_chars = last_total_chars;
            }

            if (chars_this_loop > 0 || uart_read_bytes > 0)
            {
                GPS_TASK_LOG("[GPS Task] GPS loop processed %lu characters this cycle (total: %lu, read_bytes=%lu)\n",
                             chars_this_loop,
                             total_chars,
                             uart_read_bytes);
            }
            if (uart_read_bytes != chars_this_loop && (uart_read_bytes > 0 || chars_this_loop > 0))
            {
                GPS_TASK_LOG("[GPS][COUNT_CHECK] loop=%lu read_bytes=%lu tinygps_delta=%lu total=%lu\n",
                             static_cast<unsigned long>(loop_count),
                             static_cast<unsigned long>(uart_read_bytes),
                             static_cast<unsigned long>(chars_this_loop),
                             static_cast<unsigned long>(total_chars));
            }

            if (service->gps_data_mutex_ != NULL && xSemaphoreTake(service->gps_data_mutex_, portMAX_DELAY) == pdTRUE)
            {
                bool was_valid = service->gps_state_.valid;
                bool has_fix = service->gps_adapter_.hasFix();
                uint8_t sat_count = service->gps_adapter_.satellites();
                service->gnss_sat_count_ = service->gps_adapter_.getSatellites(service->gnss_sats_, kMaxGnssSats);
                service->gnss_status_ = service->gps_adapter_.getGnssStatus();
                if (service->gnss_status_.sats_in_view == 0)
                {
                    service->gnss_status_.sats_in_view =
                        static_cast<uint8_t>(service->gnss_sat_count_);
                }
                gps::TrackPoint track_pt{};
                bool have_track_point = false;

                if (!service->gps_time_synced_)
                {
                    if (service->gps_adapter_.syncTime(kGpsUartPollIntervalMs))
                    {
                        service->gps_time_synced_ = true;
                        GPS_TASK_LOG("[GPS Task] *** TIME SYNCED TO RTC (automatic) *** (loop %lu, sat=%d)\n",
                                     loop_count, sat_count);
                    }
                }

                if (has_fix)
                {
                    const double raw_lat = service->gps_adapter_.latitude();
                    const double raw_lon = service->gps_adapter_.longitude();
                    uint32_t last_motion_ms = service->motion_policy_.lastMotionMs();
                    auto decision = service->jitter_filter_.update(raw_lat, raw_lon, now_ms, last_motion_ms);

                    if (decision.accepted)
                    {
                        service->gps_state_.lat = raw_lat;
                        service->gps_state_.lng = raw_lon;
                        service->gps_state_.has_alt = service->gps_adapter_.hasAltitude();
                        service->gps_state_.alt_m = service->gps_state_.has_alt
                                                        ? service->gps_adapter_.altitude()
                                                        : 0.0;
                        service->gps_state_.has_speed = service->gps_adapter_.hasSpeed();
                        service->gps_state_.speed_mps = service->gps_state_.has_speed
                                                            ? service->gps_adapter_.speed()
                                                            : 0.0;
                        service->gps_state_.has_course = service->gps_adapter_.hasCourse();
                        service->gps_state_.course_deg = service->gps_state_.has_course
                                                             ? service->gps_adapter_.course()
                                                             : 0.0;
                        service->gps_state_.satellites = sat_count;
                        service->gps_state_.valid = true;
                        service->gps_last_update_time_ = millis();
                        service->gps_state_.age = 0;
                        track_pt.lat = service->gps_state_.lat;
                        track_pt.lon = service->gps_state_.lng;
                        track_pt.satellites = sat_count;
                        track_pt.timestamp = time(nullptr);
                        have_track_point = true;

                        if (decision.forced)
                        {
                            log_w("[TRACK] force-accept dt=%.1fs d=%.1fm v=%.2f vmax=%.2f stationary=%d",
                                  decision.dt_s, decision.distance_m, decision.v_gps,
                                  decision.v_max, decision.stationary ? 1 : 0);
                        }

                        if (!was_valid || should_log)
                        {
                            GPS_TASK_LOG("[GPS Task] *** FIX ACQUIRED *** lat=%.6f, lng=%.6f, sat=%d (loop %lu)\n",
                                         service->gps_state_.lat, service->gps_state_.lng,
                                         service->gps_state_.satellites, loop_count);
                        }
                        if (!was_valid)
                        {
                            Serial.printf("[GPS][FIX] acquired lat=%.6f lng=%.6f sat=%u loop=%lu\n",
                                          service->gps_state_.lat,
                                          service->gps_state_.lng,
                                          static_cast<unsigned>(service->gps_state_.satellites),
                                          static_cast<unsigned long>(loop_count));
                        }
                    }
                    else
                    {
                        log_w("[TRACK] reject dt=%.1fs d=%.1fm v=%.2f vmax=%.2f stationary=%d rejects=%u",
                              decision.dt_s, decision.distance_m, decision.v_gps,
                              decision.v_max, decision.stationary ? 1 : 0,
                              decision.reject_count);
                    }
                }
                else
                {
                    service->gps_state_.valid = false;
                    service->gps_state_.has_alt = false;
                    service->gps_state_.has_speed = false;
                    service->gps_state_.has_course = false;
                    service->gps_state_.alt_m = 0.0;
                    service->gps_state_.speed_mps = 0.0;
                    service->gps_state_.course_deg = 0.0;
                    service->gps_state_.satellites = sat_count;
                    if (was_valid)
                    {
                        GPS_TASK_LOG("[GPS Task] *** FIX LOST *** (loop %lu)\n", loop_count);
                    }
                    if (should_log)
                    {
                        GPS_TASK_LOG("[GPS Task] GPS ready but no fix yet (loop %lu, sat=%d, chars_this_cycle=%lu)\n",
                                     loop_count, sat_count, chars_this_loop);
                    }
                }
                health_valid = service->gps_state_.valid;
                health_sat_count = sat_count;
                health_sats_in_view = service->gnss_status_.sats_in_view;
                health_sats_in_use = service->gnss_status_.sats_in_use;
                health_lat = service->gps_state_.lat;
                health_lng = service->gps_state_.lng;
                health_fix_age_ms =
                    (service->gps_state_.valid && service->gps_last_update_time_ > 0)
                        ? (millis() - service->gps_last_update_time_)
                        : UINT32_MAX;
                xSemaphoreGive(service->gps_data_mutex_);

                // Append GPX track points outside the GPS mutex to keep the task responsive.
                if (have_track_point && gps::TrackRecorder::getInstance().isRecording())
                {
                    gps::TrackRecorder::getInstance().appendPoint(track_pt);
                }
                gps::TrackRecorder::getInstance().flushPending(false);
            }
            else
            {
                GPS_TASK_LOG("[GPS Task] ERROR: Failed to take mutex (loop %lu)\n", loop_count);
            }
        }
        else if (service->gps_powered_)
        {
            static uint32_t last_retry_ms = 0;
            const uint32_t RETRY_INTERVAL_MS = 300000;

            if (last_retry_ms == 0 || (now_ms - last_retry_ms) >= RETRY_INTERVAL_MS)
            {
                if (should_log)
                {
                    GPS_TASK_LOG("[GPS Task] GPS not ready (loop %lu)\n", loop_count);
                }

                GPS_TASK_LOG("[GPS Task] Attempting to reinitialize GPS (last retry: %lu ms ago, loop %lu)\n",
                             last_retry_ms > 0 ? (now_ms - last_retry_ms) : 0, loop_count);

                bool retry_result = false;
                bool retry_attempted = false;

                service->takeGpsUartLock();

                const bool ready_after_lock = service->gps_adapter_.isReady();
                service->gps_ready_ = ready_after_lock;

                if (service->gps_powered_ && !service->gps_initializing_ && !ready_after_lock)
                {
                    retry_attempted = true;
                    service->gps_initializing_ = true;
                    service->gps_ready_ = false;
                    retry_result = service->gps_adapter_.init();
                    service->gps_ready_ = service->gps_adapter_.isReady() || retry_result;
                    if (retry_result)
                    {
                        const GnssRuntimeConfig gnss_config = service->runtime_config_.gnssConfig();
                        service->runtime_config_.setGnssConfig(gnss_config.mode, gnss_config.sat_mask);
                        service->applyGnssConfig();
                        service->applyInternalNmeaConfig();
                    }
                    service->gps_initializing_ = false;
                }

                service->giveGpsUartLock();
                last_retry_ms = now_ms;

                if (!retry_attempted)
                {
                    if (should_log)
                    {
                        GPS_TASK_LOG("[GPS Task] GPS reinitialization skipped; init state changed (loop %lu)\n",
                                     loop_count);
                    }
                }
                else if (retry_result)
                {
                    GPS_TASK_LOG("[GPS Task] *** GPS REINITIALIZATION SUCCESSFUL *** (loop %lu)\n", loop_count);
                    service->gps_ready_ = true;
                }
                else
                {
                    service->gps_ready_ = false;
                    GPS_TASK_LOG("[GPS Task] GPS reinitialization failed, will retry in %lu ms (loop %lu)\n",
                                 RETRY_INTERVAL_MS, loop_count);
                }
            }
        }

        if (service->gps_post_config_watch_active_)
        {
            const uint32_t watch_elapsed_ms = millis() - service->gps_post_config_start_ms_;
            if (total_chars > service->gps_post_config_chars_)
            {
                Serial.printf("[GPS] post_config stream_resumed source=%s chars_before=%lu chars_now=%lu delta=%lu elapsed_ms=%lu\n",
                              service->gps_post_config_label_,
                              static_cast<unsigned long>(service->gps_post_config_chars_),
                              static_cast<unsigned long>(total_chars),
                              static_cast<unsigned long>(total_chars - service->gps_post_config_chars_),
                              static_cast<unsigned long>(watch_elapsed_ms));
                service->gps_post_config_watch_active_ = false;
            }
            else if (watch_elapsed_ms >= kGpsPostConfigWatchMs)
            {
                const GpsDiagnosticCode code =
                    (service->gps_post_config_chars_ == 0 && total_chars == 0)
                        ? GpsDiagnosticCode::NoTraffic
                        : GpsDiagnosticCode::TrafficStalled;
                Serial.printf("[GPS] post_config no_stream source=%s code=%s chars_before=%lu chars_now=%lu elapsed_ms=%lu powered=%d ready=%d\n",
                              service->gps_post_config_label_,
                              gpsDiagnosticCodeName(code),
                              static_cast<unsigned long>(service->gps_post_config_chars_),
                              static_cast<unsigned long>(total_chars),
                              static_cast<unsigned long>(watch_elapsed_ms),
                              service->gps_powered_ ? 1 : 0,
                              gps_ready ? 1 : 0);
                service->gps_post_config_watch_active_ = false;
            }
        }

        now_ms = millis();
        if (last_health_log_ms == 0 || (now_ms - last_health_log_ms) >= kGpsHealthLogIntervalMs)
        {
            const uint32_t chars_since_log =
                (total_chars >= last_health_total_chars) ? (total_chars - last_health_total_chars) : 0;
            const uint32_t reads_since_log =
                (total_uart_reads >= last_health_total_uart_reads) ? (total_uart_reads - last_health_total_uart_reads) : 0;
            Serial.printf("[GPS] health ready=%d powered=%d state=%s lat=%.6f lng=%.6f age_ms=%lu sats=%u view=%u use=%u chars_total=%lu chars_%lus=%lu read_%lus=%lu poll_ms=%lu collection_ms=%lu loops=%lu\n",
                          gps_ready ? 1 : 0,
                          service->gps_powered_ ? 1 : 0,
                          gps_valid_text(health_valid),
                          health_lat,
                          health_lng,
                          static_cast<unsigned long>(health_fix_age_ms),
                          static_cast<unsigned>(health_sat_count),
                          static_cast<unsigned>(health_sats_in_view),
                          static_cast<unsigned>(health_sats_in_use),
                          static_cast<unsigned long>(total_chars),
                          static_cast<unsigned long>(kGpsHealthLogIntervalMs / 1000),
                          static_cast<unsigned long>(chars_since_log),
                          static_cast<unsigned long>(kGpsHealthLogIntervalMs / 1000),
                          static_cast<unsigned long>(reads_since_log),
                          static_cast<unsigned long>(kGpsUartPollIntervalMs),
                          static_cast<unsigned long>(service->getCollectionInterval()),
                          static_cast<unsigned long>(loop_count));
            last_health_total_chars = total_chars;
            last_health_total_uart_reads = total_uart_reads;
            service->gps_chars_recent_ = chars_since_log;
            last_health_log_ms = now_ms;
        }

        const uint32_t interval_ms =
            (service->gps_powered_ && gps_ready) ? kGpsUartPollIntervalMs : kGpsIdlePollIntervalMs;
        TickType_t frequency = pdMS_TO_TICKS(interval_ms);

        if (should_log)
        {
            GPS_TASK_LOG("[GPS Task] Waiting %lu ms until next cycle...\n", interval_ms);
        }

        vTaskDelayUntil(&last_wake_time, frequency);
    }
}

void GpsService::motionTask(void* pvParameters)
{
    GpsService* service = static_cast<GpsService*>(pvParameters);
    if (service == nullptr)
    {
        vTaskDelete(NULL);
        return;
    }

    TickType_t last_wake_time = xTaskGetTickCount();

    while (true)
    {
        uint32_t now_ms = millis();

        if (service->motion_adapter_.isReady() && service->motion_policy_.isEnabled())
        {
            if (service->motion_policy_.shouldUpdateSensor(now_ms))
            {
                service->motion_adapter_.update();
                service->motion_policy_.markSensorUpdated(now_ms);
            }
            service->updateMotionState(now_ms);
        }

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(service->motion_policy_.taskIntervalMs()));
    }
}

void GpsService::setGPSPowerState(bool enable)
{
    if (enable)
    {
        takeGpsUartLock();
        if (gps_powered_)
        {
            giveGpsUartLock();
            return;
        }
        gps_initializing_ = true;
        gps_ready_ = false;
        gps_adapter_.powerOn();
        gps_powered_ = true;
        bool init_ok = gps_adapter_.init();
        gps_ready_ = gps_adapter_.isReady() || init_ok;
        Serial.printf("[GPS] init: %s\n", init_ok ? "OK" : "FAIL");
        setCollectionInterval(kMinGpsCollectionIntervalMs);
        if (gps_ready_)
        {
            const GnssRuntimeConfig gnss_config = runtime_config_.gnssConfig();
            runtime_config_.setGnssConfig(gnss_config.mode, gnss_config.sat_mask);
            applyGnssConfig();
            applyInternalNmeaConfig();
        }
        gps_initializing_ = false;
        giveGpsUartLock();
        if (gps_task_handle_ != nullptr)
        {
            vTaskResume(gps_task_handle_);
        }
    }
    else
    {
        takeGpsUartLock();
        if (!gps_powered_)
        {
            gps_initializing_ = false;
            giveGpsUartLock();
            return;
        }
        gps_initializing_ = true;
        if (gps_task_handle_ != nullptr)
        {
            vTaskSuspend(gps_task_handle_);
        }
        gps_adapter_.powerOff();
        gps_powered_ = false;
        gps_ready_ = false;
        gps_post_config_watch_active_ = false;
        gps_initializing_ = false;
        giveGpsUartLock();
    }
}

void GpsService::updateMotionState(uint32_t now_ms)
{
    if (gps_disabled_ || !user_enabled_ || gps_board_ == nullptr)
    {
        return;
    }

    const GpsPowerInputs power_inputs = runtime_state_.makePowerInputs(
        motion_policy_.isEnabled(), motion_config_.idle_timeout_ms, motion_policy_.lastMotionMs());

    const GpsPowerDecision decision = decideGpsPower(power_inputs, now_ms);

    runtime_state_.setMotionControlArmedMs(decision.motion_control_armed_ms);
    const bool should_enable_gps = decision.should_enable_gps;

    if (should_enable_gps && !gps_powered_)
    {
        setGPSPowerState(true);
        setCollectionInterval(runtime_state_.requestedCollectionIntervalMs());
    }
    else if (!should_enable_gps && gps_powered_)
    {
        setGPSPowerState(false);
    }
}

} // namespace gps
