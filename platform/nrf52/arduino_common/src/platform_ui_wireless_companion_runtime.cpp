#include "platform/ui/wireless_companion_runtime.h"

#include <cstddef>
#include <cstdio>

namespace platform::ui::wireless_companion
{
namespace
{

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

} // namespace

bool is_supported()
{
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    out.state = State::Unsupported;
    copy_text(out.message, sizeof(out.message), "C6 companion unsupported");
    copy_text(out.detail, sizeof(out.detail), "nrf52_target_has_no_wireless_companion");
    return out;
}

bool enter_download_mode()
{
    return false;
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
    (void)state;
    return "unsupported";
}

} // namespace platform::ui::wireless_companion
