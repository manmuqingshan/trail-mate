#include "platform/ui/time_runtime.h"

#include "platform/ui/settings_store.h"
#include "platform/ui/timezone_profile.h"

#include <cstdlib>
#include <ctime>
#include <limits>

namespace platform::ui::time
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kTimezoneOffsetKey = "timezone_offset";
constexpr const char* kTimezoneProfileKey = "timezone_profile";
constexpr const char* kTimezoneOffsetEnv = "TRAIL_MATE_TZ_OFFSET_MIN";
constexpr int kUnsetSetting = std::numeric_limits<int>::min() / 2;

bool env_configured(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
}

int env_timezone_offset_or_default(int fallback)
{
    const char* value = std::getenv(kTimezoneOffsetEnv);
    if (!value || value[0] == '\0')
    {
        return fallback;
    }

    char* end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || (end && *end != '\0'))
    {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool timezone_setting_configured()
{
    if (env_configured(kTimezoneOffsetEnv))
    {
        return true;
    }
    return ::platform::ui::settings_store::get_int(
               kSettingsNs, kTimezoneOffsetKey, kUnsetSetting) != kUnsetSetting ||
           ::platform::ui::settings_store::get_int(
               kSettingsNs, kTimezoneProfileKey, kUnsetSetting) != kUnsetSetting;
}

int system_timezone_offset_min_at(::time_t utc_seconds)
{
    std::tm local_tm{};
    std::tm utc_tm{};

    const std::tm* local = std::localtime(&utc_seconds);
    const std::tm* utc = std::gmtime(&utc_seconds);
    if (local == nullptr || utc == nullptr)
    {
        return 0;
    }

    local_tm = *local;
    utc_tm = *utc;
    const std::time_t local_as_local = std::mktime(&local_tm);
    const std::time_t utc_as_local = std::mktime(&utc_tm);
    if (local_as_local == static_cast<std::time_t>(-1) ||
        utc_as_local == static_cast<std::time_t>(-1))
    {
        return 0;
    }

    return static_cast<int>(
        std::difftime(local_as_local, utc_as_local) / 60.0);
}

} // namespace

int timezone_offset_min()
{
    if (!timezone_setting_configured())
    {
        return system_timezone_offset_min_at(::time(nullptr));
    }

    const int fixed_offset_min = env_timezone_offset_or_default(
        ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneOffsetKey, 0));
    return timezone_offset_for_profile_id_at(timezone_profile_id(), fixed_offset_min, ::time(nullptr));
}

void set_timezone_offset_min(int offset_min)
{
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneOffsetKey, offset_min);
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            kTimezoneProfileKey,
                                            timezone_profile_id_for_fixed_offset(offset_min));
}

int timezone_profile_id()
{
    if (!timezone_setting_configured())
    {
        return timezone_profile_id_for_offset(timezone_offset_min());
    }

    const int offset = env_timezone_offset_or_default(
        ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneOffsetKey, 0));
    const int profile_id = ::platform::ui::settings_store::get_int(
        kSettingsNs,
        kTimezoneProfileKey,
        timezone_profile_id_for_legacy_offset(offset));
    return timezone_profile_id_is_fixed(profile_id) || timezone_profile_by_id(profile_id)
               ? profile_id
               : timezone_profile_id_for_legacy_offset(offset);
}

void set_timezone_profile_id(int profile_id)
{
    const TimezoneProfile* profile = timezone_profile_by_id(profile_id);
    if (!profile)
    {
        profile = default_timezone_profile();
    }
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneProfileKey, profile->id);
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneOffsetKey, profile->standard_offset_min);
}

::time_t apply_timezone_offset(::time_t utc_seconds)
{
    if (!timezone_setting_configured())
    {
        return utc_seconds +
               static_cast<::time_t>(
                   system_timezone_offset_min_at(utc_seconds)) *
                   60;
    }

    const int fixed_offset_min = env_timezone_offset_or_default(
        ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneOffsetKey, 0));
    return utc_seconds +
           static_cast<time_t>(timezone_offset_for_profile_id_at(timezone_profile_id(), fixed_offset_min, utc_seconds)) *
               60;
}

::time_t apply_timezone_offset_for_utc(::time_t utc_seconds)
{
    return apply_timezone_offset(utc_seconds);
}

bool localtime_now(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }

    const ::time_t now = ::time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    if (!timezone_setting_configured())
    {
        const ::tm* tmp = ::std::localtime(&now);
        if (!tmp)
        {
            return false;
        }
        *out_tm = *tmp;
        return true;
    }

    const ::time_t local = apply_timezone_offset(now);
    const ::tm* tmp = ::gmtime(&local);
    if (!tmp)
    {
        return false;
    }

    *out_tm = *tmp;
    return true;
}

bool set_utc_time(::time_t utc_seconds)
{
    (void)utc_seconds;
    return false;
}

} // namespace platform::ui::time
