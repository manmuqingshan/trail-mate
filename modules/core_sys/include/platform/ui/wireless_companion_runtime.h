#pragma once

#include <cstdint>

namespace platform::ui::wireless_companion
{

enum class State : uint8_t
{
    Unsupported = 0,
    NotStarted,
    Missing,
    TransportPending,
    Present,
    Error,
};

struct Status
{
    bool supported = false;
    bool board_capable = false;
    bool started = false;
    bool present = false;
    State state = State::Unsupported;
    uint16_t protocol_min = 1;
    uint16_t protocol_max = 1;
    uint16_t selected_protocol = 0;
    uint32_t supported_features = 0;
    uint32_t enabled_features = 0;
    uint32_t firmware_version = 0;
    uint32_t free_heap = 0;
    uint32_t config_seq = 0;
    uint16_t config_error = 0;
    uint16_t selected_mtu = 0;
    uint8_t ble_state = 0;
    uint8_t espnow_state = 0;
    uint8_t wifi_state = 0;
    uint32_t ble_uplink_count = 0;
    uint32_t ble_event_count = 0;
    uint32_t espnow_uplink_count = 0;
    uint32_t espnow_event_count = 0;
    uint32_t wifi_event_count = 0;
    char message[96] = {};
    char detail[128] = {};
};

bool is_supported();
Status status();
const char* state_name(State state);
const char* service_state_name(uint8_t state);

} // namespace platform::ui::wireless_companion
