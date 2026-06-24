#include "platform/ui/wireless_companion_runtime.h"

#include "platform/esp/idf_common/wireless_companion/c6_companion.h"

#include <cstddef>
#include <cstdio>

namespace platform::ui::wireless_companion
{
namespace
{

namespace c6 = ::platform::esp::idf_common::wireless_companion;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

State map_state(c6::CompanionState state)
{
    switch (state)
    {
    case c6::CompanionState::Unsupported:
        return State::Unsupported;
    case c6::CompanionState::NotStarted:
        return State::NotStarted;
    case c6::CompanionState::Missing:
        return State::Missing;
    case c6::CompanionState::TransportPending:
        return State::TransportPending;
    case c6::CompanionState::Present:
        return State::Present;
    case c6::CompanionState::Error:
        return State::Error;
    }
    return State::Error;
}

const char* message_for_state(State state)
{
    switch (state)
    {
    case State::Unsupported:
        return "C6 companion unsupported";
    case State::NotStarted:
        return "C6 companion not started";
    case State::Missing:
        return "C6 companion missing";
    case State::TransportPending:
        return "C6 transport pending";
    case State::Present:
        return "C6 companion present";
    case State::Error:
        return "C6 companion error";
    }
    return "C6 companion unknown";
}

} // namespace

bool is_supported()
{
    return status().supported;
}

Status status()
{
    const c6::C6CompanionStatus c6_status = c6::get_c6_companion_status();

    Status out{};
    out.supported = c6_status.board_capable;
    out.board_capable = c6_status.board_capable;
    out.started = c6_status.started;
    out.present = c6_status.present;
    out.state = map_state(c6_status.state);
    out.protocol_min = c6_status.protocol_min;
    out.protocol_max = c6_status.protocol_max;
    out.selected_protocol = c6_status.selected_protocol;
    out.supported_features = c6_status.supported_features;
    out.enabled_features = c6_status.enabled_features;
    out.firmware_version = c6_status.firmware_version;
    out.free_heap = c6_status.free_heap;
    out.config_seq = c6_status.config_seq;
    out.config_error = c6_status.config_error;
    out.selected_mtu = c6_status.selected_mtu;
    out.ble_state = c6_status.ble_state;
    out.espnow_state = c6_status.espnow_state;
    out.wifi_state = c6_status.wifi_state;
    out.ble_uplink_count = c6_status.ble_uplink_count;
    out.ble_event_count = c6_status.ble_event_count;
    out.espnow_uplink_count = c6_status.espnow_uplink_count;
    out.espnow_event_count = c6_status.espnow_event_count;
    out.wifi_event_count = c6_status.wifi_event_count;
    copy_text(out.message, sizeof(out.message), message_for_state(out.state));
    copy_text(out.detail, sizeof(out.detail), c6_status.detail);
    return out;
}

const char* state_name(State state)
{
    switch (state)
    {
    case State::Unsupported:
        return "unsupported";
    case State::NotStarted:
        return "not_started";
    case State::Missing:
        return "missing";
    case State::TransportPending:
        return "transport_pending";
    case State::Present:
        return "present";
    case State::Error:
        return "error";
    }
    return "unknown";
}

const char* service_state_name(uint8_t state)
{
    switch (state)
    {
    case 0:
        return "disabled";
    case 1:
        return "starting";
    case 2:
        return "ready";
    case 3:
        return "error";
    case 4:
        return "not_configured";
    default:
        return "unknown";
    }
}

} // namespace platform::ui::wireless_companion
