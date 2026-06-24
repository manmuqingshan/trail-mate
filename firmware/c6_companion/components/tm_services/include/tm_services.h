#pragma once

#include "hostlink/c6/c6_protocol.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef bool (*tm_services_hostlink_send_fn)(uint8_t frame_type,
                                                 uint8_t channel,
                                                 uint16_t flags,
                                                 uint16_t ack,
                                                 const uint8_t* payload,
                                                 size_t payload_len,
                                                 void* user);

    typedef struct tm_services_hostlink
    {
        tm_services_hostlink_send_fn send;
        void* user;
    } tm_services_hostlink_t;

    esp_err_t tm_services_init(void);
    void tm_services_bind_hostlink(const tm_services_hostlink_t* hostlink);
    uint32_t tm_services_supported_features(void);
    uint32_t tm_services_enabled_features(void);
    uint16_t tm_services_selected_mtu(void);
    tm_c6_service_state_t tm_services_ble_state(void);
    tm_c6_service_state_t tm_services_espnow_state(void);
    tm_c6_service_state_t tm_services_wifi_state(void);

    esp_err_t tm_services_apply_config(const tm_c6_companion_config_t* config,
                                       tm_c6_config_report_t* out_report);
    void tm_services_mark_ble_configured(esp_err_t result, const char* detail);
    void tm_services_mark_espnow_configured(esp_err_t result, const char* detail);
    void tm_services_mark_wifi_configured(esp_err_t result, const char* detail);
    void tm_services_fill_config_report(uint32_t config_seq,
                                        uint16_t error_code,
                                        const char* detail,
                                        tm_c6_config_report_t* out_report);
    void tm_services_fill_diag_report(uint8_t hostlink_state, tm_c6_diag_report_t* out_report);

    bool tm_services_send_ble_uplink(uint8_t profile,
                                     uint8_t connection_id,
                                     const uint8_t* payload,
                                     size_t payload_len);
    bool tm_services_send_ble_event(const tm_c6_ble_event_t* event);
    bool tm_services_send_espnow_uplink(const tm_c6_espnow_packet_t* packet);
    bool tm_services_send_espnow_event(const tm_c6_espnow_event_t* event);
    bool tm_services_send_wifi_event(const tm_c6_wifi_event_t* event);
    bool tm_services_send_log(const char* message);
    void tm_services_flush_logs(void);

    bool tm_services_can_accept_wireless_rx(const char* detail);
    void tm_services_note_ble_downlink(void);
    void tm_services_note_espnow_tx(void);
    void tm_services_record_error(uint16_t error_code, const char* detail);

#ifdef __cplusplus
}
#endif
