#include "platform/ui/time_runtime.h"

#include "ui/ui_common.h"

#include <sys/time.h>

namespace platform::ui::time
{
namespace
{

constexpr ::time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC

bool is_valid_epoch(::time_t value)
{
    return value >= kMinValidEpochSeconds;
}

} // namespace

int timezone_offset_min()
{
    return ui_get_timezone_offset_min();
}

void set_timezone_offset_min(int offset_min)
{
    ui_set_timezone_offset_min(offset_min);
}

int timezone_profile_id()
{
    return ui_get_timezone_profile_id();
}

void set_timezone_profile_id(int profile_id)
{
    ui_set_timezone_profile_id(profile_id);
}

time_t apply_timezone_offset(time_t utc_seconds)
{
    return ui_apply_timezone_offset(utc_seconds);
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
    const ::time_t now = ::time(nullptr);
    if (!is_valid_epoch(now))
    {
        return false;
    }
    const ::time_t local = apply_timezone_offset(now);
    const tm* tmp = gmtime(&local);
    if (!tmp)
    {
        return false;
    }
    *out_tm = *tmp;
    return true;
}

bool set_utc_time(time_t utc_seconds)
{
    if (!is_valid_epoch(utc_seconds))
    {
        return false;
    }
    timeval tv{};
    tv.tv_sec = utc_seconds;
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
}

} // namespace platform::ui::time
