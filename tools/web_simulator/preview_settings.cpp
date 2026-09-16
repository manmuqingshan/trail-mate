#include "platform/ui/device_runtime.h"
#include "platform/ui/gps_runtime.h"
#include "platform/ui/settings_store.h"
#include "platform/ui/wifi_runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
namespace platform::ui::settings_store
{
static std::map<std::string, std::string> values;
static std::string key_for(const char* ns, const char* key) { return std::string(ns) + "/" + key; }
int get_int(const char* ns, const char* key, int fallback)
{
    auto i = values.find(key_for(ns, key));
    return i == values.end() ? fallback : std::strtol(i->second.c_str(), nullptr, 10);
}
uint32_t get_uint(const char* ns, const char* key, uint32_t fallback)
{
    auto i = values.find(key_for(ns, key));
    return i == values.end() ? fallback : std::strtoul(i->second.c_str(), nullptr, 10);
}
bool get_bool(const char* ns, const char* key, bool fallback) { return get_int(ns, key, fallback ? 1 : 0) != 0; }
void put_int(const char* ns, const char* key, int value) { values[key_for(ns, key)] = std::to_string(value); }
void put_uint(const char* ns, const char* key, uint32_t value) { values[key_for(ns, key)] = std::to_string(value); }
void put_bool(const char* ns, const char* key, bool value) { put_int(ns, key, value ? 1 : 0); }
bool put_string(const char* ns, const char* key, const char* value)
{
    values[key_for(ns, key)] = value ? value : "";
    return true;
}
bool get_string(const char* ns, const char* key, std::string& out)
{
    auto i = values.find(key_for(ns, key));
    if (i == values.end()) return false;
    out = i->second;
    return true;
}
void remove_keys(const char* ns, const char* const* keys, std::size_t count)
{
    for (size_t i = 0; i < count; ++i) values.erase(key_for(ns, keys[i]));
}
void clear_namespace(const char* ns)
{
    const auto prefix = std::string(ns) + "/";
    for (auto i = values.begin(); i != values.end();)
        if (i->first.compare(0, prefix.size(), prefix) == 0) i = values.erase(i);
        else ++i;
}
void begin_change_batch() {}
void end_change_batch() {}
} // namespace platform::ui::settings_store
namespace platform::ui::device
{
static uint8_t tone_volume = 60;
uint8_t default_message_tone_volume() { return tone_volume; }
void set_message_tone_volume(uint8_t value) { tone_volume = value; }
void play_message_tone() {}
void trigger_haptic() {}
bool supports_configurable_battery_gauge() { return true; }
void reload_configurable_battery_gauge() {}
void restart() {}
} // namespace platform::ui::device
namespace platform::ui::gps
{
bool supports_receiver_baud_setting() { return true; }
bool supports_receiver_init_policy_settings() { return true; }
bool supports_gnss_runtime_settings() { return true; }
bool supports_collection_interval_setting() { return true; }
bool supports_altitude_reference_setting() { return true; }
bool supports_coordinate_format_setting() { return true; }
bool supports_external_nmea_output_setting() { return true; }
void set_enabled(bool value) { platform::ui::settings_store::put_bool("preview", "gps", value); }
void set_collection_interval(uint32_t value) { platform::ui::settings_store::put_uint("preview", "gps_interval", value); }
void set_receiver_init_config(const GpsReceiverInitConfig&) {}
void set_gnss_config(uint8_t, uint8_t) {}
void set_power_strategy(uint8_t) {}
void set_external_nmea_config(uint8_t, uint8_t) {}
} // namespace platform::ui::gps
namespace platform::ui::wifi
{
bool find_saved_config(const char*, Config& out) { return load_config(out); }
bool scan(ScanResult* out, std::size_t capacity, std::size_t& count)
{
    count = capacity ? 1 : 0;
    if (count)
    {
        std::snprintf(out[0].ssid, sizeof(out[0].ssid), "Preview network");
        out[0].rssi = -55;
        out[0].requires_password = false;
    }
    return true;
}
} // namespace platform::ui::wifi
