#pragma once

#include <cstdint>
#include <ctime>

namespace platform::ui::time
{

int timezone_offset_min();
void set_timezone_offset_min(int offset_min);
int timezone_profile_id();
void set_timezone_profile_id(int profile_id);
time_t apply_timezone_offset(time_t utc_seconds);
time_t apply_timezone_offset_for_utc(time_t utc_seconds);
bool localtime_now(struct tm* out_tm);
bool set_utc_time(time_t utc_seconds);

} // namespace platform::ui::time
