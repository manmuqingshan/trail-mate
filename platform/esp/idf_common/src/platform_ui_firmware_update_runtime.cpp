#include "platform/ui/firmware_update_runtime.h"

#include <cstdio>

namespace platform::ui::firmware_update
{

bool is_supported()
{
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    out.phase = Phase::Unsupported;
    std::snprintf(out.message, sizeof(out.message), "%s", "OTA unsupported");
    return out;
}

bool start_check()
{
    return false;
}

bool start_install()
{
    return false;
}

} // namespace platform::ui::firmware_update
