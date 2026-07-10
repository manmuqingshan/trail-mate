#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)

#include "platform/ui/wifi_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "hostlink/c6/c6_protocol.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/ui/settings_store.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4) || defined(TRAIL_MATE_ESP_BOARD_TAB5)
#define TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN 1
#endif

#if defined(ESP_PLATFORM) && defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace platform::ui::wifi
{
namespace
{

namespace c6 = ::platform::esp::idf_common::wireless_companion;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kWifiEnabledKey = "wifi_enabled";
constexpr const char* kWifiSsidKey = "wifi_ssid";
constexpr const char* kWifiPasswordKey = "wifi_password";
constexpr const char* kWifiProfileCountKey = "wifi_prof_count";
constexpr std::size_t kWifiProfileCapacity = 10;
constexpr uint32_t kWifiFeatureMask = TM_C6_FEATURE_WIFI_STA | TM_C6_FEATURE_WIFI_AP;
#if defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
constexpr uint32_t kC6ScanWaitTimeoutMs = 10000;
constexpr uint32_t kC6ScanPollIntervalMs = 100;
#endif

struct RuntimeState
{
    bool profiles_cached = false;
    Config profiles[kWifiProfileCapacity] = {};
    std::size_t profile_count = 0;
    std::size_t next_profile_index = 0;
};

RuntimeState s_runtime{};

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

bool has_saved_credentials(const Config& config)
{
    return config.ssid[0] != '\0';
}

void profile_key(char* out, std::size_t out_len, const char* field, std::size_t index)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "wifi_%s_%u", field ? field : "profile", static_cast<unsigned>(index));
}

bool same_ssid(const Config& lhs, const Config& rhs)
{
    return std::strncmp(lhs.ssid, rhs.ssid, sizeof(lhs.ssid)) == 0;
}

int profile_index_for_ssid(const char* ssid)
{
    if (!ssid || ssid[0] == '\0')
    {
        return -1;
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (std::strncmp(s_runtime.profiles[i].ssid, ssid, sizeof(s_runtime.profiles[i].ssid)) == 0)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void clear_profiles()
{
    for (Config& profile : s_runtime.profiles)
    {
        profile = Config{};
    }
    s_runtime.profile_count = 0;
    s_runtime.profiles_cached = true;
}

void clamp_next_profile_index()
{
    if (s_runtime.profile_count == 0)
    {
        s_runtime.next_profile_index = 0;
        return;
    }
    s_runtime.next_profile_index %= s_runtime.profile_count;
}

void append_profile_unique(const Config& profile)
{
    if (!has_saved_credentials(profile))
    {
        return;
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (same_ssid(s_runtime.profiles[i], profile))
        {
            return;
        }
    }
    if (s_runtime.profile_count >= kWifiProfileCapacity)
    {
        return;
    }
    s_runtime.profiles[s_runtime.profile_count] = profile;
    s_runtime.profiles[s_runtime.profile_count].enabled = true;
    ++s_runtime.profile_count;
}

void upsert_profile_front(const Config& profile)
{
    if (!has_saved_credentials(profile))
    {
        return;
    }

    std::size_t existing = s_runtime.profile_count;
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (same_ssid(s_runtime.profiles[i], profile))
        {
            existing = i;
            break;
        }
    }

    std::size_t last = s_runtime.profile_count;
    if (existing < s_runtime.profile_count)
    {
        last = existing;
    }
    else if (s_runtime.profile_count < kWifiProfileCapacity)
    {
        last = s_runtime.profile_count;
        ++s_runtime.profile_count;
    }
    else
    {
        last = kWifiProfileCapacity - 1U;
    }

    for (std::size_t i = last; i > 0; --i)
    {
        s_runtime.profiles[i] = s_runtime.profiles[i - 1U];
    }
    s_runtime.profiles[0] = profile;
    s_runtime.profiles[0].enabled = true;
    s_runtime.next_profile_index = 0;
    s_runtime.profiles_cached = true;
}

bool persist_profiles()
{
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            kWifiProfileCountKey,
                                            static_cast<int>(s_runtime.profile_count));
    bool ok = true;
    for (std::size_t i = 0; i < kWifiProfileCapacity; ++i)
    {
        char ssid_key[24] = {};
        char password_key[24] = {};
        profile_key(ssid_key, sizeof(ssid_key), "ssid", i);
        profile_key(password_key, sizeof(password_key), "password", i);
        const Config& profile = s_runtime.profiles[i];
        ok = ::platform::ui::settings_store::put_string(
                 kSettingsNs,
                 ssid_key,
                 i < s_runtime.profile_count ? profile.ssid : "") &&
             ok;
        ok = ::platform::ui::settings_store::put_string(
                 kSettingsNs,
                 password_key,
                 i < s_runtime.profile_count ? profile.password : "") &&
             ok;
    }
    return ok;
}

bool read_profile_from_store(std::size_t index, Config& profile)
{
    profile = Config{};
    profile.enabled = true;

    char ssid_key[24] = {};
    char password_key[24] = {};
    profile_key(ssid_key, sizeof(ssid_key), "ssid", index);
    profile_key(password_key, sizeof(password_key), "password", index);

    std::string value;
    if (!::platform::ui::settings_store::get_string(kSettingsNs, ssid_key, value))
    {
        return false;
    }
    copy_text(profile.ssid, sizeof(profile.ssid), value.c_str());
    if (!has_saved_credentials(profile))
    {
        return false;
    }

    value.clear();
    if (::platform::ui::settings_store::get_string(kSettingsNs, password_key, value))
    {
        copy_text(profile.password, sizeof(profile.password), value.c_str());
    }
    else
    {
        char legacy_password_key[24] = {};
        profile_key(legacy_password_key, sizeof(legacy_password_key), "pass", index);
        value.clear();
        if (::platform::ui::settings_store::get_string(kSettingsNs, legacy_password_key, value))
        {
            copy_text(profile.password, sizeof(profile.password), value.c_str());
            (void)::platform::ui::settings_store::put_string(
                kSettingsNs,
                password_key,
                profile.password);
            const char* legacy_keys[] = {legacy_password_key};
            ::platform::ui::settings_store::remove_keys(kSettingsNs, legacy_keys, 1);
        }
    }

    return true;
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
    const bool has_primary_credentials = has_saved_credentials(out);
    const std::size_t next_profile_index = s_runtime.next_profile_index;
    clear_profiles();
    s_runtime.next_profile_index = next_profile_index;
    const int stored_count = std::clamp(
        ::platform::ui::settings_store::get_int(kSettingsNs, kWifiProfileCountKey, 0),
        0,
        static_cast<int>(kWifiProfileCapacity));
    const int profile_read_count =
        stored_count > 0 ? stored_count : static_cast<int>(kWifiProfileCapacity);
    std::size_t loaded_profile_count = 0;
    for (int i = 0; i < profile_read_count; ++i)
    {
        Config profile{};
        if (read_profile_from_store(static_cast<std::size_t>(i), profile))
        {
            append_profile_unique(profile);
            ++loaded_profile_count;
        }
    }
    append_profile_unique(out);
    if (stored_count == 0 && loaded_profile_count > 0)
    {
        std::printf("[WiFi][C6] recovered saved profile count=%u from profile keys\n",
                    static_cast<unsigned>(s_runtime.profile_count));
        (void)persist_profiles();
    }
    clamp_next_profile_index();
    if (!has_primary_credentials && s_runtime.profile_count > 0)
    {
        copy_text(out.ssid, sizeof(out.ssid), s_runtime.profiles[0].ssid);
        copy_text(out.password, sizeof(out.password), s_runtime.profiles[0].password);
    }
    return out;
}

bool save_saved_config(const Config& config)
{
    if (!s_runtime.profiles_cached)
    {
        (void)load_saved_config();
    }
    upsert_profile_front(config);
    const bool profiles_ok = persist_profiles();
    const bool ssid_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiSsidKey, config.ssid);
    const bool password_ok =
        ::platform::ui::settings_store::put_string(kSettingsNs, kWifiPasswordKey, config.password);
    ::platform::ui::settings_store::put_bool(kSettingsNs, kWifiEnabledKey, config.enabled);
    return profiles_ok && ssid_ok && password_ok;
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

void append_scan_results(std::vector<ScanResult>& out_results, const c6::C6CompanionStatus& c6_status)
{
    for (uint8_t i = 0; i < c6_status.wifi_scan_result_count; ++i)
    {
        ScanResult result{};
        copy_text(result.ssid, sizeof(result.ssid), c6_status.wifi_scan_results[i].ssid);
        result.rssi = c6_status.wifi_scan_results[i].rssi;
        result.requires_password = c6_status.wifi_scan_results[i].authmode != 0;
        out_results.push_back(result);
    }
}

bool wait_for_c6_scan(std::vector<ScanResult>& out_results)
{
#if defined(ESP_PLATFORM) && defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
    const TickType_t start_ticks = xTaskGetTickCount();
    const TickType_t timeout_ticks = pdMS_TO_TICKS(kC6ScanWaitTimeoutMs);
    bool saw_scanning = true;

    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks)
    {
        const auto c6_status = c6::get_c6_companion_status();
        if (!c6_status.present)
        {
            return false;
        }
        if (c6_status.wifi_scanning)
        {
            saw_scanning = true;
        }
        else if (saw_scanning || c6_status.wifi_scan_result_count > 0)
        {
            append_scan_results(out_results, c6_status);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(kC6ScanPollIntervalMs));
    }

    const auto c6_status = c6::get_c6_companion_status();
    append_scan_results(out_results, c6_status);
    return !out_results.empty();
#else
    (void)out_results;
    return false;
#endif
}

bool select_auto_profile_from_scan(std::size_t& out_index)
{
    out_index = s_runtime.next_profile_index;
    if (s_runtime.profile_count == 0 || !c6_present())
    {
        return false;
    }

    std::vector<ScanResult> results;
    append_scan_results(results, c6::get_c6_companion_status());
    if (results.empty())
    {
        const bool sent = c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Scan));
        if (!sent)
        {
            return false;
        }
#if defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
        results.clear();
        if (!wait_for_c6_scan(results))
        {
            return false;
        }
#endif
    }

    int best_index = -1;
    int best_rssi = -128;
    for (const ScanResult& result : results)
    {
        const int index = profile_index_for_ssid(result.ssid);
        if (index < 0)
        {
            continue;
        }
        if (best_index < 0 || result.rssi > best_rssi)
        {
            best_index = index;
            best_rssi = result.rssi;
        }
    }
    if (best_index < 0)
    {
        std::printf("[WiFi][C6] auto scan no saved match results=%u profiles=%u\n",
                    static_cast<unsigned>(results.size()),
                    static_cast<unsigned>(s_runtime.profile_count));
        return false;
    }

    out_index = static_cast<std::size_t>(best_index);
    s_runtime.next_profile_index = out_index;
    std::printf("[WiFi][C6] auto scan matched profile index=%u/%u ssid=%s rssi=%d results=%u\n",
                static_cast<unsigned>(out_index + 1U),
                static_cast<unsigned>(s_runtime.profile_count),
                s_runtime.profiles[out_index].ssid,
                best_rssi,
                static_cast<unsigned>(results.size()));
    return true;
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

bool find_saved_config(const char* ssid, Config& out)
{
    out = Config{};
    if (!ssid || ssid[0] == '\0')
    {
        return false;
    }
    if (!s_runtime.profiles_cached)
    {
        (void)load_saved_config();
    }
    for (std::size_t i = 0; i < s_runtime.profile_count; ++i)
    {
        if (std::strncmp(s_runtime.profiles[i].ssid, ssid, sizeof(s_runtime.profiles[i].ssid)) == 0)
        {
            out = s_runtime.profiles[i];
            return true;
        }
    }
    return false;
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
    if (!override_config && s_runtime.profile_count > 0)
    {
        std::size_t index = s_runtime.next_profile_index % s_runtime.profile_count;
        (void)select_auto_profile_from_scan(index);
        config = s_runtime.profiles[index];
        config.enabled = true;
        std::printf("[WiFi][C6] auto connect profile index=%u/%u ssid=%s\n",
                    static_cast<unsigned>(index + 1U),
                    static_cast<unsigned>(s_runtime.profile_count),
                    config.ssid);
        s_runtime.next_profile_index = (index + 1U) % s_runtime.profile_count;
    }
    config.enabled = true;
    if (override_config && !save_saved_config(config))
    {
        return false;
    }
    if (!has_saved_credentials(config) || !c6_present())
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
    append_scan_results(out_results, c6_status);
    if (!c6_status.present)
    {
        return !out_results.empty();
    }
    const bool sent = c6::c6_companion().sendWifiControl(make_control(c6::WifiCommand::Scan));
    if (!sent)
    {
        return false;
    }
#if defined(TRAIL_MATE_WIFI_RUNTIME_C6_ASYNC_SCAN)
    out_results.clear();
    return wait_for_c6_scan(out_results);
#else
    return true;
#endif
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
