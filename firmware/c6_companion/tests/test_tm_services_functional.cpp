#include "tm_diag.h"
#include "tm_services.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

struct CapturedFrame
{
    uint8_t frame_type = 0;
    uint8_t channel = 0;
    uint16_t flags = 0;
    uint16_t ack = 0;
    std::array<uint8_t, TM_C6_MAX_PAYLOAD> payload{};
    std::size_t payload_len = 0;
};

CapturedFrame g_last_frame;
std::size_t g_send_count = 0;
bool g_send_result = true;
std::size_t g_free_heap = 96U * 1024U;
std::size_t g_minimum_free_heap = 48U * 1024U;
int64_t g_time_us = 123456789;

void reset_capture()
{
    g_last_frame = CapturedFrame{};
    g_send_count = 0;
    g_send_result = true;
}

bool capture_send(uint8_t frame_type,
                  uint8_t channel,
                  uint16_t flags,
                  uint16_t ack,
                  const uint8_t* payload,
                  size_t payload_len,
                  void* user)
{
    (void)user;
    g_last_frame.frame_type = frame_type;
    g_last_frame.channel = channel;
    g_last_frame.flags = flags;
    g_last_frame.ack = ack;
    g_last_frame.payload_len = payload_len;
    if (payload_len > 0)
    {
        assert(payload != nullptr);
        assert(payload_len <= g_last_frame.payload.size());
        std::memcpy(g_last_frame.payload.data(), payload, payload_len);
    }
    ++g_send_count;
    return g_send_result;
}

void bind_capture_hostlink()
{
    tm_services_hostlink_t hostlink{};
    hostlink.send = capture_send;
    tm_services_bind_hostlink(&hostlink);
}

bool has_feature(uint32_t value, uint32_t mask)
{
    return (value & mask) == mask;
}

} // namespace

extern "C"
{

    size_t heap_caps_get_free_size(uint32_t caps)
    {
        (void)caps;
        return g_free_heap;
    }

    size_t heap_caps_get_minimum_free_size(uint32_t caps)
    {
        (void)caps;
        return g_minimum_free_heap;
    }

    int64_t esp_timer_get_time(void)
    {
        return g_time_us;
    }

} // extern "C"

int main()
{
    assert(tm_diag_init() == ESP_OK);
    assert(tm_services_init() == ESP_OK);

    const uint32_t expected_supported =
        TM_C6_FEATURE_BLE_MESHTASTIC | TM_C6_FEATURE_BLE_MESHCORE |
        TM_C6_FEATURE_BLE_TRAILMATE | TM_C6_FEATURE_ESPNOW_TEAM |
        TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP |
        TM_C6_FEATURE_DIAG_LOG | TM_C6_FEATURE_HOSTLINK_PING;
    const uint32_t supported = tm_services_supported_features();
    assert(has_feature(supported, expected_supported));
    assert((supported & TM_C6_FEATURE_SLAVE_OTA) == 0);
    assert(tm_services_enabled_features() == 0);
    assert(tm_services_selected_mtu() == TM_C6_MAX_PAYLOAD);

    tm_c6_companion_config_t config{};
    config.config_seq = 42;
    config.ble.ble_enabled = 1;
    config.ble.meshtastic_enabled = 1;
    config.ble.trailmate_enabled = 1;
    config.ble.preferred_mtu = TM_C6_MAX_PAYLOAD + 100;
    config.espnow.espnow_enabled = 1;
    config.wifi.wifi_enabled = 1;
    config.wifi.sta_enabled = 1;
    config.wifi.ap_enabled = 1;

    tm_c6_config_report_t report{};
    assert(tm_services_apply_config(&config, &report) == ESP_OK);
    assert(report.config_seq == 42);
    assert(report.supported_features == supported);
    assert(has_feature(report.enabled_features,
                       TM_C6_FEATURE_BLE_MESHTASTIC |
                           TM_C6_FEATURE_BLE_TRAILMATE |
                           TM_C6_FEATURE_ESPNOW_TEAM |
                           TM_C6_FEATURE_WIFI_STA |
                           TM_C6_FEATURE_WIFI_AP));
    assert(report.selected_mtu == TM_C6_MAX_PAYLOAD);
    assert(report.ble_state == TM_C6_SERVICE_STARTING);
    assert(report.espnow_state == TM_C6_SERVICE_STARTING);
    assert(report.wifi_state == TM_C6_SERVICE_STARTING);
    assert(std::strcmp(report.detail, "config_applied") == 0);

    tm_services_mark_ble_configured(ESP_OK, nullptr);
    tm_services_mark_espnow_configured(ESP_OK, nullptr);
    tm_services_mark_wifi_configured(ESP_ERR_INVALID_STATE, "wifi_down");
    assert(tm_services_ble_state() == TM_C6_SERVICE_READY);
    assert(tm_services_espnow_state() == TM_C6_SERVICE_READY);
    assert(tm_services_wifi_state() == TM_C6_SERVICE_ERROR);
    assert(!has_feature(tm_services_enabled_features(),
                        TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP));
    assert(has_feature(tm_services_enabled_features(),
                       TM_C6_FEATURE_BLE_MESHTASTIC |
                           TM_C6_FEATURE_BLE_TRAILMATE |
                           TM_C6_FEATURE_ESPNOW_TEAM));

    bind_capture_hostlink();
    reset_capture();
    const uint8_t ble_payload[] = {0x11, 0x22, 0x33};
    assert(tm_services_send_ble_uplink(TM_C6_BLE_PROFILE_MESHCORE,
                                       7,
                                       ble_payload,
                                       sizeof(ble_payload)));
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_BLE_UPLINK);
    assert(g_last_frame.channel == TM_C6_CH_BLE_MESHCORE);
    assert(g_last_frame.payload_len == sizeof(tm_c6_ble_packet_header_t) + sizeof(ble_payload));
    tm_c6_ble_packet_header_t ble_header{};
    std::memcpy(&ble_header, g_last_frame.payload.data(), sizeof(ble_header));
    assert(ble_header.profile == TM_C6_BLE_PROFILE_MESHCORE);
    assert(ble_header.connection_id == 7);
    assert(ble_header.payload_len == sizeof(ble_payload));
    assert(std::memcmp(g_last_frame.payload.data() + sizeof(ble_header),
                       ble_payload,
                       sizeof(ble_payload)) == 0);

    reset_capture();
    tm_c6_espnow_packet_t espnow_packet{};
    espnow_packet.channel = 6;
    espnow_packet.rssi_valid = 1;
    espnow_packet.rssi = -55;
    espnow_packet.payload_len = 2;
    espnow_packet.payload[0] = 0xA5;
    espnow_packet.payload[1] = 0x5A;
    assert(tm_services_send_espnow_uplink(&espnow_packet));
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_ESPNOW_UPLINK);
    assert(g_last_frame.channel == TM_C6_CH_ESPNOW_TEAM);
    assert(g_last_frame.payload_len == sizeof(tm_c6_espnow_packet_t));

    reset_capture();
    tm_c6_wifi_event_t wifi_event{};
    wifi_event.event_kind = TM_C6_WIFI_EVENT_STA_GOT_IP;
    wifi_event.ipv4_addr = 0x01020304;
    assert(tm_services_send_wifi_event(&wifi_event));
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_WIFI_EVENT);
    assert(g_last_frame.channel == TM_C6_CH_WIFI_MGMT);

    reset_capture();
    tm_c6_wifi_time_sync_t time_sync{};
    time_sync.epoch_seconds = 1700000000;
    time_sync.error_code = TM_C6_OK;
    std::memcpy(time_sync.source, "c6_wifi_sntp", 12);
    assert(tm_services_send_wifi_time_sync(&time_sync));
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_WIFI_TIME_SYNC);
    assert(g_last_frame.channel == TM_C6_CH_WIFI_MGMT);
    assert(g_last_frame.payload_len == sizeof(tm_c6_wifi_time_sync_t));

    tm_c6_diag_report_t diag{};
    tm_services_note_ble_downlink();
    tm_services_note_espnow_tx();
    tm_services_fill_diag_report(3, &diag);
    assert(diag.uptime_ms == 123456);
    assert(diag.free_heap == g_free_heap);
    assert(diag.minimum_free_heap == g_minimum_free_heap);
    assert(diag.ble_uplink_count == 1);
    assert(diag.ble_downlink_count == 1);
    assert(diag.espnow_rx_count == 1);
    assert(diag.espnow_tx_count == 1);
    assert(diag.wifi_event_count == 1);
    assert(diag.hostlink_state == 3);
    assert(diag.last_error_code == TM_C6_ERROR_INTERNAL);
    assert(std::strcmp(diag.last_error, "wifi_down") == 0);

    assert(tm_diag_init() == ESP_OK);
    tm_services_bind_hostlink(nullptr);
    assert(!tm_services_send_log("offline_log"));
    char log_line[64] = {};
    assert(tm_diag_pop_log(log_line, sizeof(log_line)));
    assert(std::strcmp(log_line, "offline_log") == 0);
    tm_diag_record_log("queued_log");
    bind_capture_hostlink();
    reset_capture();
    tm_services_flush_logs();
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_LOG);
    assert(g_last_frame.channel == TM_C6_CH_DIAG);
    assert(std::memcmp(g_last_frame.payload.data(), "queued_log", 10) == 0);

    g_free_heap = 24U * 1024U;
    assert(tm_diag_init() == ESP_OK);
    reset_capture();
    assert(tm_services_can_accept_wireless_rx("rx_pressure"));
    assert(g_send_count == 1);
    assert(g_last_frame.frame_type == TM_C6_FRAME_LOG);
    reset_capture();
    assert(tm_services_can_accept_wireless_rx("rx_pressure_again"));
    assert(g_send_count == 0);

    g_free_heap = 64U * 1024U;
    assert(tm_services_can_accept_wireless_rx("heap_recovered"));
    g_free_heap = 8U * 1024U;
    reset_capture();
    assert(!tm_services_can_accept_wireless_rx("rx_stop"));
    tm_services_fill_diag_report(0, &diag);
    assert(diag.last_error_code == TM_C6_ERROR_LOW_MEMORY);
    assert(std::strcmp(diag.last_error, "rx_stop") == 0);

    std::array<uint8_t, TM_C6_MAX_PAYLOAD> too_large_payload{};
    assert(!tm_services_send_ble_uplink(TM_C6_BLE_PROFILE_TRAILMATE,
                                        1,
                                        too_large_payload.data(),
                                        too_large_payload.size()));
    tm_services_fill_diag_report(0, &diag);
    assert(diag.last_error_code == TM_C6_ERROR_PAYLOAD_TOO_LARGE);
    assert(std::strcmp(diag.last_error, "ble_uplink_too_large") == 0);

    return 0;
}
