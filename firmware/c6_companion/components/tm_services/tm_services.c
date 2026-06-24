#include "tm_services.h"

#include "tm_diag.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static tm_services_hostlink_t s_hostlink;
static tm_c6_companion_config_t s_config;
static uint32_t s_enabled_features;
static uint16_t s_selected_mtu = TM_C6_MAX_PAYLOAD;
static tm_c6_service_state_t s_ble_state = TM_C6_SERVICE_DISABLED;
static tm_c6_service_state_t s_espnow_state = TM_C6_SERVICE_DISABLED;
static tm_c6_service_state_t s_wifi_state = TM_C6_SERVICE_DISABLED;
static uint32_t s_ble_uplink_count;
static uint32_t s_ble_downlink_count;
static uint32_t s_espnow_rx_count;
static uint32_t s_espnow_tx_count;
static uint32_t s_wifi_event_count;
static uint16_t s_last_error_code;
static char s_last_error[64];
static bool s_low_memory_warning_sent;

enum
{
    TM_C6_LOW_MEMORY_WARNING_BYTES = 32 * 1024,
    TM_C6_LOW_MEMORY_STOP_BYTES = 16 * 1024,
};

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool send_frame(uint8_t frame_type, uint8_t channel, const uint8_t* payload, size_t payload_len)
{
    if (s_hostlink.send == NULL)
    {
        return false;
    }
    return s_hostlink.send(frame_type, channel, 0, 0, payload, payload_len, s_hostlink.user);
}

static void copy_text(char* out, size_t out_len, const char* text)
{
    if (out == NULL || out_len == 0)
    {
        return;
    }
    snprintf(out, out_len, "%s", text ? text : "");
}

static uint32_t configured_ble_feature_mask(void)
{
    uint32_t features = 0;
    if (s_config.ble.ble_enabled)
    {
        if (s_config.ble.meshtastic_enabled)
        {
            features |= TM_C6_FEATURE_BLE_MESHTASTIC;
        }
        if (s_config.ble.meshcore_enabled)
        {
            features |= TM_C6_FEATURE_BLE_MESHCORE;
        }
        if (s_config.ble.trailmate_enabled)
        {
            features |= TM_C6_FEATURE_BLE_TRAILMATE;
        }
    }
    return features;
}

static uint32_t configured_wifi_feature_mask(void)
{
    uint32_t features = 0;
    if (s_config.wifi.wifi_enabled)
    {
        if (s_config.wifi.sta_enabled)
        {
            features |= TM_C6_FEATURE_WIFI_STA;
        }
        if (s_config.wifi.ap_enabled)
        {
            features |= TM_C6_FEATURE_WIFI_AP;
        }
    }
    return features;
}

esp_err_t tm_services_init(void)
{
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_hostlink, 0, sizeof(s_hostlink));
    s_enabled_features = 0;
    s_selected_mtu = TM_C6_MAX_PAYLOAD;
    s_ble_state = TM_C6_SERVICE_DISABLED;
    s_espnow_state = TM_C6_SERVICE_DISABLED;
    s_wifi_state = TM_C6_SERVICE_DISABLED;
    s_ble_uplink_count = 0;
    s_ble_downlink_count = 0;
    s_espnow_rx_count = 0;
    s_espnow_tx_count = 0;
    s_wifi_event_count = 0;
    s_last_error_code = TM_C6_OK;
    s_last_error[0] = '\0';
    s_low_memory_warning_sent = false;
    return ESP_OK;
}

void tm_services_bind_hostlink(const tm_services_hostlink_t* hostlink)
{
    if (hostlink == NULL)
    {
        memset(&s_hostlink, 0, sizeof(s_hostlink));
        return;
    }
    s_hostlink = *hostlink;
}

uint32_t tm_services_supported_features(void)
{
    return TM_C6_FEATURE_BLE_MESHTASTIC | TM_C6_FEATURE_BLE_MESHCORE |
           TM_C6_FEATURE_BLE_TRAILMATE | TM_C6_FEATURE_ESPNOW_TEAM |
           TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP |
           TM_C6_FEATURE_DIAG_LOG | TM_C6_FEATURE_HOSTLINK_PING;
}

uint32_t tm_services_enabled_features(void)
{
    return s_enabled_features;
}

uint16_t tm_services_selected_mtu(void)
{
    return s_selected_mtu;
}

tm_c6_service_state_t tm_services_ble_state(void)
{
    return s_ble_state;
}

tm_c6_service_state_t tm_services_espnow_state(void)
{
    return s_espnow_state;
}

tm_c6_service_state_t tm_services_wifi_state(void)
{
    return s_wifi_state;
}

esp_err_t tm_services_apply_config(const tm_c6_companion_config_t* config,
                                   tm_c6_config_report_t* out_report)
{
    if (config == NULL)
    {
        tm_services_record_error(TM_C6_ERROR_INTERNAL, "null_config");
        tm_services_fill_config_report(0, TM_C6_ERROR_INTERNAL, "null_config", out_report);
        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;
    s_enabled_features = 0;

    if (config->ble.ble_enabled)
    {
        if (config->ble.meshtastic_enabled)
        {
            s_enabled_features |= TM_C6_FEATURE_BLE_MESHTASTIC;
        }
        if (config->ble.meshcore_enabled)
        {
            s_enabled_features |= TM_C6_FEATURE_BLE_MESHCORE;
        }
        if (config->ble.trailmate_enabled)
        {
            s_enabled_features |= TM_C6_FEATURE_BLE_TRAILMATE;
        }
        s_ble_state = configured_ble_feature_mask() ? TM_C6_SERVICE_STARTING : TM_C6_SERVICE_DISABLED;
    }
    else
    {
        s_ble_state = TM_C6_SERVICE_DISABLED;
    }

    if (config->espnow.espnow_enabled)
    {
        s_enabled_features |= TM_C6_FEATURE_ESPNOW_TEAM;
        s_espnow_state = TM_C6_SERVICE_STARTING;
    }
    else
    {
        s_espnow_state = TM_C6_SERVICE_DISABLED;
    }

    if (config->wifi.wifi_enabled)
    {
        if (config->wifi.sta_enabled)
        {
            s_enabled_features |= TM_C6_FEATURE_WIFI_STA;
        }
        if (config->wifi.ap_enabled)
        {
            s_enabled_features |= TM_C6_FEATURE_WIFI_AP;
        }
        s_wifi_state = configured_wifi_feature_mask() ? TM_C6_SERVICE_STARTING : TM_C6_SERVICE_DISABLED;
    }
    else
    {
        s_wifi_state = TM_C6_SERVICE_DISABLED;
    }

    s_enabled_features &= tm_services_supported_features();
    s_selected_mtu = config->ble.preferred_mtu;
    if (s_selected_mtu == 0 || s_selected_mtu > TM_C6_MAX_PAYLOAD)
    {
        s_selected_mtu = TM_C6_MAX_PAYLOAD;
    }

    s_last_error_code = TM_C6_OK;
    s_last_error[0] = '\0';
    tm_services_fill_config_report(config->config_seq, TM_C6_OK, "config_applied", out_report);
    return ESP_OK;
}

void tm_services_mark_ble_configured(esp_err_t result, const char* detail)
{
    const uint32_t mask = configured_ble_feature_mask();
    if (mask == 0)
    {
        s_ble_state = TM_C6_SERVICE_DISABLED;
        return;
    }
    if (result == ESP_OK)
    {
        s_ble_state = TM_C6_SERVICE_READY;
        return;
    }
    s_ble_state = TM_C6_SERVICE_ERROR;
    s_enabled_features &= ~mask;
    tm_services_record_error(TM_C6_ERROR_INTERNAL, detail ? detail : "ble_config_failed");
}

void tm_services_mark_espnow_configured(esp_err_t result, const char* detail)
{
    if (!s_config.espnow.espnow_enabled)
    {
        s_espnow_state = TM_C6_SERVICE_DISABLED;
        return;
    }
    if (result == ESP_OK)
    {
        s_espnow_state = TM_C6_SERVICE_READY;
        return;
    }
    s_espnow_state = TM_C6_SERVICE_ERROR;
    s_enabled_features &= ~TM_C6_FEATURE_ESPNOW_TEAM;
    tm_services_record_error(TM_C6_ERROR_INTERNAL, detail ? detail : "espnow_config_failed");
}

void tm_services_mark_wifi_configured(esp_err_t result, const char* detail)
{
    const uint32_t mask = configured_wifi_feature_mask();
    if (mask == 0)
    {
        s_wifi_state = TM_C6_SERVICE_DISABLED;
        return;
    }
    if (result == ESP_OK)
    {
        s_wifi_state = TM_C6_SERVICE_READY;
        return;
    }
    s_wifi_state = TM_C6_SERVICE_ERROR;
    s_enabled_features &= ~mask;
    tm_services_record_error(TM_C6_ERROR_INTERNAL, detail ? detail : "wifi_config_failed");
}

void tm_services_fill_config_report(uint32_t config_seq,
                                    uint16_t error_code,
                                    const char* detail,
                                    tm_c6_config_report_t* out_report)
{
    if (out_report == NULL)
    {
        return;
    }
    memset(out_report, 0, sizeof(*out_report));
    out_report->config_seq = config_seq;
    out_report->supported_features = tm_services_supported_features();
    out_report->enabled_features = s_enabled_features;
    out_report->c6_free_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out_report->error_code = error_code;
    out_report->ble_state = (uint8_t)s_ble_state;
    out_report->espnow_state = (uint8_t)s_espnow_state;
    out_report->wifi_state = (uint8_t)s_wifi_state;
    out_report->selected_mtu = s_selected_mtu;
    copy_text(out_report->detail, sizeof(out_report->detail), detail);
}

void tm_services_fill_diag_report(uint8_t hostlink_state, tm_c6_diag_report_t* out_report)
{
    if (out_report == NULL)
    {
        return;
    }
    memset(out_report, 0, sizeof(*out_report));
    out_report->uptime_ms = uptime_ms();
    out_report->free_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    out_report->minimum_free_heap = (uint32_t)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    out_report->supported_features = tm_services_supported_features();
    out_report->enabled_features = s_enabled_features;
    out_report->ble_uplink_count = s_ble_uplink_count;
    out_report->ble_downlink_count = s_ble_downlink_count;
    out_report->espnow_rx_count = s_espnow_rx_count;
    out_report->espnow_tx_count = s_espnow_tx_count;
    out_report->wifi_event_count = s_wifi_event_count;
    out_report->last_error_code = s_last_error_code;
    out_report->hostlink_state = hostlink_state;
    copy_text(out_report->last_error, sizeof(out_report->last_error), s_last_error);
}

bool tm_services_send_ble_uplink(uint8_t profile,
                                 uint8_t connection_id,
                                 const uint8_t* payload,
                                 size_t payload_len)
{
    if (payload_len > (TM_C6_MAX_PAYLOAD - sizeof(tm_c6_ble_packet_header_t)) ||
        (payload == NULL && payload_len != 0))
    {
        tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "ble_uplink_too_large");
        return false;
    }

    uint8_t frame_payload[TM_C6_MAX_PAYLOAD] = {};
    tm_c6_ble_packet_header_t header = {
        .profile = profile,
        .connection_id = connection_id,
        .payload_len = (uint16_t)payload_len,
    };
    memcpy(frame_payload, &header, sizeof(header));
    if (payload_len > 0)
    {
        memcpy(frame_payload + sizeof(header), payload, payload_len);
    }
    ++s_ble_uplink_count;
    return send_frame(TM_C6_FRAME_BLE_UPLINK,
                      profile == TM_C6_BLE_PROFILE_MESHTASTIC
                          ? TM_C6_CH_BLE_MESHTASTIC
                      : profile == TM_C6_BLE_PROFILE_MESHCORE
                          ? TM_C6_CH_BLE_MESHCORE
                          : TM_C6_CH_BLE_TRAILMATE,
                      frame_payload,
                      sizeof(header) + payload_len);
}

bool tm_services_send_ble_event(const tm_c6_ble_event_t* event)
{
    if (event == NULL)
    {
        return false;
    }
    return send_frame(TM_C6_FRAME_BLE_EVENT, TM_C6_CH_CONTROL, (const uint8_t*)event, sizeof(*event));
}

bool tm_services_send_espnow_uplink(const tm_c6_espnow_packet_t* packet)
{
    if (packet == NULL)
    {
        return false;
    }
    ++s_espnow_rx_count;
    return send_frame(TM_C6_FRAME_ESPNOW_UPLINK,
                      TM_C6_CH_ESPNOW_TEAM,
                      (const uint8_t*)packet,
                      sizeof(*packet));
}

bool tm_services_send_espnow_event(const tm_c6_espnow_event_t* event)
{
    if (event == NULL)
    {
        return false;
    }
    return send_frame(TM_C6_FRAME_ESPNOW_EVENT, TM_C6_CH_ESPNOW_TEAM, (const uint8_t*)event, sizeof(*event));
}

bool tm_services_send_wifi_event(const tm_c6_wifi_event_t* event)
{
    if (event == NULL)
    {
        return false;
    }
    ++s_wifi_event_count;
    return send_frame(TM_C6_FRAME_WIFI_EVENT, TM_C6_CH_WIFI_MGMT, (const uint8_t*)event, sizeof(*event));
}

bool tm_services_send_log(const char* message)
{
    const bool sent =
        send_frame(TM_C6_FRAME_LOG, TM_C6_CH_DIAG, (const uint8_t*)message, message ? strlen(message) : 0);
    if (!sent)
    {
        tm_diag_record_log(message);
    }
    return sent;
}

void tm_services_flush_logs(void)
{
    enum
    {
        TM_C6_LOG_FLUSH_MAX_PER_REQUEST = 8,
    };

    char message[128] = {};
    for (uint8_t count = 0; count < TM_C6_LOG_FLUSH_MAX_PER_REQUEST; ++count)
    {
        if (!tm_diag_pop_log(message, sizeof(message)))
        {
            return;
        }
        if (!send_frame(TM_C6_FRAME_LOG, TM_C6_CH_DIAG, (const uint8_t*)message, strlen(message)))
        {
            tm_diag_record_log(message);
            return;
        }
    }
}

bool tm_services_can_accept_wireless_rx(const char* detail)
{
    const uint32_t free_heap = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (free_heap < TM_C6_LOW_MEMORY_WARNING_BYTES)
    {
        tm_services_record_error(TM_C6_ERROR_LOW_MEMORY, detail ? detail : "low_memory_warning");
        if (!s_low_memory_warning_sent)
        {
            (void)tm_services_send_log("TM_C6_EVENT_LOW_MEMORY");
            s_low_memory_warning_sent = true;
        }
    }
    else
    {
        s_low_memory_warning_sent = false;
    }

    if (free_heap < TM_C6_LOW_MEMORY_STOP_BYTES)
    {
        tm_services_record_error(TM_C6_ERROR_LOW_MEMORY, detail ? detail : "low_memory_stop");
        return false;
    }
    return true;
}

void tm_services_note_ble_downlink(void)
{
    ++s_ble_downlink_count;
}

void tm_services_note_espnow_tx(void)
{
    ++s_espnow_tx_count;
}

void tm_services_record_error(uint16_t error_code, const char* detail)
{
    s_last_error_code = error_code;
    copy_text(s_last_error, sizeof(s_last_error), detail);
    tm_diag_record_log(detail);
}
