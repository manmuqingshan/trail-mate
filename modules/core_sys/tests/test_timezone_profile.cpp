#include "platform/ui/timezone_profile.h"

#include <cassert>
#include <cstdint>
#include <ctime>
#include <string>

namespace
{

std::time_t utc_epoch(int year, int month, int day, int hour, int minute, int second)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned shifted_month = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy = (153 * shifted_month + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
    const int64_t seconds_of_day =
        static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + second;
    return static_cast<std::time_t>(days * 86400 + seconds_of_day);
}

} // namespace

int main()
{
    using namespace platform::ui::time;

    const auto* eastern = timezone_profile_by_tzdef("EST5EDT,M3.2.0/2,M11.1.0/2");
    assert(eastern != nullptr);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 1, 15, 12, 0, 0)) == -300);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 5, 22, 12, 0, 0)) == -240);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 11, 15, 12, 0, 0)) == -300);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 3, 8, 6, 59, 59)) == -300);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 3, 8, 7, 0, 0)) == -240);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 11, 1, 5, 59, 59)) == -240);
    assert(timezone_offset_for_profile_at(*eastern, utc_epoch(2026, 11, 1, 6, 0, 0)) == -300);
    assert(timezone_profile_id_for_legacy_offset(-300) == eastern->id);

    const auto* central = timezone_profile_by_tzdef("CST6CDT,M3.2.0/2,M11.1.0/2");
    assert(central != nullptr);
    assert(timezone_offset_for_profile_at(*central, utc_epoch(2026, 5, 22, 12, 0, 0)) == -300);
    assert(timezone_offset_for_profile_at(*central, utc_epoch(2026, 3, 8, 7, 59, 59)) == -360);
    assert(timezone_offset_for_profile_at(*central, utc_epoch(2026, 3, 8, 8, 0, 0)) == -300);
    assert(timezone_offset_for_profile_at(*central, utc_epoch(2026, 11, 1, 6, 59, 59)) == -300);
    assert(timezone_offset_for_profile_at(*central, utc_epoch(2026, 11, 1, 7, 0, 0)) == -360);
    assert(timezone_profile_id_for_legacy_offset(-360) == central->id);
    assert(timezone_profile_id_for_legacy_offset(0) == timezone_profile_by_tzdef("UTC0")->id);
    assert(timezone_profile_id_for_legacy_offset(60) == timezone_profile_by_tzdef("CET-1CEST,M3.5.0/2,M10.5.0/3")->id);

    const auto* phoenix = timezone_profile_by_tzdef("MST7");
    assert(phoenix != nullptr);
    assert(timezone_offset_for_profile_at(*phoenix, utc_epoch(2026, 1, 15, 12, 0, 0)) == -420);
    assert(timezone_offset_for_profile_at(*phoenix, utc_epoch(2026, 5, 22, 12, 0, 0)) == -420);
    assert(timezone_profile_id_for_offset(-420) == phoenix->id);
    assert(timezone_profile_id_for_legacy_offset(-420) != phoenix->id);
    assert(timezone_offset_for_profile_id_at(kFixedTimezoneProfileId, -195, utc_epoch(2026, 5, 22, 12, 0, 0)) ==
           -195);

    int parsed = 0;
    assert(parse_posix_tz_standard_offset_minutes("EST5EDT,M3.2.0/2,M11.1.0/2", &parsed));
    assert(parsed == -300);
    assert(parse_posix_tz_standard_offset_minutes("UTC+5:30", &parsed));
    assert(parsed == -330);

    char fixed[24] = {};
    build_fixed_posix_tzdef(-300, fixed, sizeof(fixed));
    assert(std::string(fixed) == "UTC+5");

    return 0;
}
