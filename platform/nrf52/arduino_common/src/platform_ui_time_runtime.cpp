#include "platform/ui/settings_store.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/timezone_profile.h"
#include "sys/clock.h"

#include <ctime>

namespace platform::ui::time
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kTimezoneKey = "timezone_offset";
constexpr const char* kTimezoneProfileKey = "timezone_profile";
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC

int stored_timezone_offset_min()
{
    return ::platform::ui::settings_store::get_int(kSettingsNs, kTimezoneKey, 0);
}

} // namespace

int timezone_offset_min()
{
    return timezone_offset_for_profile_id_at(timezone_profile_id(),
                                             stored_timezone_offset_min(),
                                             static_cast<time_t>(sys::epoch_seconds_now()));
}

void set_timezone_offset_min(int offset_min)
{
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneKey, offset_min);
    ::platform::ui::settings_store::put_int(kSettingsNs,
                                            kTimezoneProfileKey,
                                            timezone_profile_id_for_fixed_offset(offset_min));
}

int timezone_profile_id()
{
    const int offset = stored_timezone_offset_min();
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
    ::platform::ui::settings_store::put_int(kSettingsNs, kTimezoneKey, profile->standard_offset_min);
}

time_t apply_timezone_offset(time_t utc_seconds)
{
    if (utc_seconds <= 0)
    {
        return utc_seconds;
    }
    return utc_seconds +
           static_cast<time_t>(timezone_offset_for_profile_id_at(timezone_profile_id(),
                                                                 stored_timezone_offset_min(),
                                                                 utc_seconds)) *
               60;
}

time_t apply_timezone_offset_for_utc(time_t utc_seconds)
{
    return apply_timezone_offset(utc_seconds);
}

bool localtime_now(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }
    const time_t utc_now = static_cast<time_t>(sys::epoch_seconds_now());
    if (utc_now < kMinValidEpochSeconds)
    {
        return false;
    }
    const time_t now = apply_timezone_offset(utc_now);
    const tm* tmp = gmtime(&now);
    if (!tmp)
    {
        return false;
    }
    *out_tm = *tmp;
    return true;
}

bool set_utc_time(time_t utc_seconds)
{
    (void)utc_seconds;
    return false;
}

} // namespace platform::ui::time
