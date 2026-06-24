#include "platform/ui/hostlink_runtime.h"

namespace platform::ui::hostlink
{

bool is_supported()
{
    return false;
}

void start()
{
}

void stop()
{
}

bool is_active()
{
    return false;
}

Status get_status()
{
    Status status{};
    status.state = LinkState::Stopped;
    return status;
}

} // namespace platform::ui::hostlink
