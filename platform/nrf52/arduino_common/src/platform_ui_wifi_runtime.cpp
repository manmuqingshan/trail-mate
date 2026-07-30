#include "platform/ui/wifi_runtime.h"

#include <cstdio>

namespace platform::ui::wifi
{

bool is_supported()
{
    return false;
}

bool load_config(Config& out)
{
    out = Config{};
    return false;
}

bool save_config(const Config& config)
{
    (void)config;
    return false;
}

bool find_saved_config(const char* ssid, Config& out)
{
    (void)ssid;
    out = Config{};
    return false;
}

bool apply_enabled(bool enabled)
{
    (void)enabled;
    return false;
}

bool connect(const Config* override_config)
{
    (void)override_config;
    return false;
}

void disconnect()
{
}

bool scan(ScanResult* out_results, std::size_t capacity, std::size_t& out_count)
{
    (void)out_results;
    (void)capacity;
    out_count = 0;
    return false;
}

Status status()
{
    Status out{};
    out.supported = false;
    out.state = ConnectionState::Unsupported;
    std::snprintf(out.message, sizeof(out.message), "%s", "Wi-Fi unsupported");
    return out;
}

} // namespace platform::ui::wifi
