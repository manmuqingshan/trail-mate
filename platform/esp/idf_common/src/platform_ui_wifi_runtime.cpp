#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/ui/wifi_runtime.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "hostlink/c6/c6_protocol.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/settings_store.h"

namespace platform::ui::wifi
{
namespace
{

namespace c6 = ::platform::esp::idf_common::wireless_companion;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr uint32_t kWifiFeatureMask = TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void copy_config_text(char* out, std::size_t out_len, const char* text)
{
    copy_text(out, out_len, text && text[0] != '\0' ? text : "");
}

void format_ipv4(uint32_t addr, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (addr == 0)
    {
        out[0] = '\0';
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%u.%u.%u.%u",
                  static_cast<unsigned>(addr & 0xffu),
                  static_cast<unsigned>((addr >> 8) & 0xffu),
                  static_cast<unsigned>((addr >> 16) & 0xffu),
                  static_cast<unsigned>((addr >> 24) & 0xffu));
}

Config load_saved_config()
{
    Config out{};
    out.enabled = ::platform::ui::settings_store::get_bool(kSettingsNs, kWifiEnabledKey, false);

    std::string value;
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiSsidKey, value))
    {
        copy_text(out.ssid, sizeof(out.ssid), value.c_str());
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, kWifiPasswordKey, value))
    {
        copy_text(out.password, sizeof(out.password), value.c_str());
    }
    return out;
}

bool save_saved_config(const Config& config)
{
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    return ssid_ok && password_ok;
}

c6::WifiCompanionConfig make_companion_wifi_config(const Config& config)
{
    c6::WifiCompanionConfig out{};
    out.enabled = config.enabled;
    out.sta_enabled = config.enabled && config.ssid[0] != '\0';
    out.ap_enabled = false;
    out.persist_credentials = false;
    copy_config_text(out.sta_ssid, sizeof(out.sta_ssid), config.ssid);
    copy_config_text(out.sta_password, sizeof(out.sta_password), config.password);
    copy_text(out.ap_ssid, sizeof(out.ap_ssid), "TrailMate-C6");
    out.ap_channel = 1;
    return out;
}

c6::WifiControl make_control(c6::WifiCommand command, const Config* config = nullptr)
{
    c6::WifiControl out{};
    out.command = command;
    if (config)
    {
        copy_config_text(out.ssid, sizeof(out.ssid), config->ssid);
        copy_config_text(out.password, sizeof(out.password), config->password);
    }
    return out;
}

bool c6_present()
{
    return c6::get_c6_companion_status().present;
}

} // namespace

bool is_supported()
{
    return c6::get_c6_companion_status().board_capable;
}

bool load_config(Config& out)
{
    out = load_saved_config();
    return true;
}

bool save_config(const Config& config)
{
    return save_saved_config(config);
}

bool apply_enabled(bool enabled)
{
    Config config = load_saved_config();
    config.enabled = enabled;
    const bool saved = save_saved_config(config);
    if (!c6_present())
    {
        return saved && !enabled;
    }
    return saved && c6::c6_companion().configureWifi(make_companion_wifi_config(config));
}

bool connect(const Config* override_config)
{
    Config config = override_config ? *override_config : load_saved_config();
    config.enabled = true;
    if (!save_saved_config(config) || !c6_present())
    {
        return false;
    }
    if (!c6::c6_companion().configureWifi(make_companion_wifi_config(config)))
    {
        return false;
    }
    return c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Connect, &config));
}

void disconnect()
{
    if (c6_present())
    {
        (void)c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Disconnect));
    }
}

bool scan(std::vector<ScanResult>& out_results)
{
    out_results.clear();
    const auto c6_status = c6::get_c6_companion_status();
    for (uint8_t i = 0; i < c6_status.wifi_scan_result_count; ++i)
    {
        ScanResult result{};
        copy_text(result.ssid, sizeof(result.ssid), c6_status.wifi_scan_results[i].ssid);
        result.rssi = c6_status.wifi_scan_results[i].rssi;
        result.requires_password = c6_status.wifi_scan_results[i].authmode != 0;
        out_results.push_back(result);
    }
    if (!c6_status.present)
    {
        return !out_results.empty();
    }
    return c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Scan));
}

Status status()
{
    const auto c6_status = c6::get_c6_companion_status();
    const Config config = load_saved_config();
    Status out{};
    out.supported = c6_status.board_capable;
    out.enabled = config.enabled;
    out.connected = c6_status.wifi_connected;
    out.scanning = c6_status.wifi_scanning;
    out.has_credentials = config.ssid[0] != '\0';
    out.rssi = -127;
    copy_text(out.ssid, sizeof(out.ssid), c6_status.wifi_ssid[0] != '\0' ? c6_status.wifi_ssid : config.ssid);
    format_ipv4(c6_status.wifi_ipv4_addr, out.ip, sizeof(out.ip));
    if (!c6_status.board_capable)
    {
        out.state = ConnectionState::Unsupported;
        copy_text(out.message, sizeof(out.message), "C6 Wi-Fi companion unsupported on this target");
    }
    else if (!c6_status.present)
    {
        out.state = ConnectionState::Error;
        copy_text(out.message, sizeof(out.message), "C6 companion not present");
    }
    else if ((c6_status.supported_features & kWifiFeatureMask) == 0)
    {
        out.state = ConnectionState::Unsupported;
        copy_text(out.message, sizeof(out.message), "C6 firmware does not support Wi-Fi management");
    }
    else if (!config.enabled)
    {
        out.state = ConnectionState::Disabled;
        copy_text(out.message, sizeof(out.message), "Wi-Fi disabled");
    }
    else if (c6_status.wifi_scanning)
    {
        out.state = ConnectionState::Scanning;
        copy_text(out.message, sizeof(out.message), "C6 Wi-Fi scan running");
    }
    else if (c6_status.wifi_connected)
    {
        out.state = ConnectionState::Connected;
        copy_text(out.message, sizeof(out.message), "C6 Wi-Fi connected");
    }
    else if (c6_status.wifi_error != 0)
    {
        out.state = ConnectionState::Error;
        copy_text(out.message, sizeof(out.message), "C6 Wi-Fi error");
    }
    else
    {
        out.state = ConnectionState::Idle;
        copy_text(out.message, sizeof(out.message), "C6 Wi-Fi idle");
    }
    return out;
}

} // namespace platform::ui::wifi

#else

#include "platform/esp/common/wifi_runtime_impl.h"

#endif
