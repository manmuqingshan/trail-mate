#include "platform/ui/timezone_profile.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace platform::ui::time
{
namespace
{

constexpr int kUtc = 0;
constexpr int kBeijing = 1;
constexpr int kTaipei = 2;
constexpr int kHongKong = 3;
constexpr int kTokyo = 4;
constexpr int kSeoul = 5;
constexpr int kSingapore = 6;
constexpr int kBangkok = 7;
constexpr int kKolkata = 8;
constexpr int kDubai = 9;
constexpr int kLondon = 10;
constexpr int kBerlin = 11;
constexpr int kParis = 12;
constexpr int kRome = 13;
constexpr int kMoscow = 14;
constexpr int kUsEastern = 15;
constexpr int kUsCentral = 16;
constexpr int kUsMountain = 17;
constexpr int kUsPacific = 18;
constexpr int kPhoenix = 19;
constexpr int kSaoPaulo = 20;
constexpr int kSydney = 21;
constexpr int kMelbourne = 22;
constexpr int kAuckland = 23;

constexpr TimezoneProfile kProfiles[] = {
    {kUtc, "UTC", "UTC0", 0, 0, DstRuleKind::None},
    {kBeijing, "Beijing (UTC+8)", "CST-8", 480, 480, DstRuleKind::None},
    {kTaipei, "Taipei (UTC+8)", "CST-8", 480, 480, DstRuleKind::None},
    {kHongKong, "Hong Kong (UTC+8)", "HKT-8", 480, 480, DstRuleKind::None},
    {kTokyo, "Tokyo (UTC+9)", "JST-9", 540, 540, DstRuleKind::None},
    {kSeoul, "Seoul (UTC+9)", "KST-9", 540, 540, DstRuleKind::None},
    {kSingapore, "Singapore (UTC+8)", "SGT-8", 480, 480, DstRuleKind::None},
    {kBangkok, "Bangkok (UTC+7)", "ICT-7", 420, 420, DstRuleKind::None},
    {kKolkata, "Kolkata (UTC+5:30)", "IST-5:30", 330, 330, DstRuleKind::None},
    {kDubai, "Dubai (UTC+4)", "GST-4", 240, 240, DstRuleKind::None},
    {kLondon, "London (UK, DST)", "GMT0BST,M3.5.0/1,M10.5.0/2", 0, 60, DstRuleKind::Eu},
    {kBerlin, "Berlin (EU, DST)", "CET-1CEST,M3.5.0/2,M10.5.0/3", 60, 120, DstRuleKind::Eu},
    {kParis, "Paris (EU, DST)", "CET-1CEST,M3.5.0/2,M10.5.0/3", 60, 120, DstRuleKind::Eu},
    {kRome, "Rome (EU, DST)", "CET-1CEST,M3.5.0/2,M10.5.0/3", 60, 120, DstRuleKind::Eu},
    {kMoscow, "Moscow (UTC+3)", "MSK-3", 180, 180, DstRuleKind::None},
    {kUsEastern, "Eastern (US, DST)", "EST5EDT,M3.2.0/2,M11.1.0/2", -300, -240, DstRuleKind::Us},
    {kUsCentral, "Central (US, DST)", "CST6CDT,M3.2.0/2,M11.1.0/2", -360, -300, DstRuleKind::Us},
    {kUsMountain, "Mountain (US, DST)", "MST7MDT,M3.2.0/2,M11.1.0/2", -420, -360, DstRuleKind::Us},
    {kUsPacific, "Pacific (US, DST)", "PST8PDT,M3.2.0/2,M11.1.0/2", -480, -420, DstRuleKind::Us},
    {kPhoenix, "Phoenix (UTC-7)", "MST7", -420, -420, DstRuleKind::None},
    {kSaoPaulo, "Sao Paulo (UTC-3)", "BRT3", -180, -180, DstRuleKind::None},
    {kSydney, "Sydney (AU, DST)", "AEST-10AEDT,M10.1.0/2,M4.1.0/3", 600, 660, DstRuleKind::Australia},
    {kMelbourne, "Melbourne (AU, DST)", "AEST-10AEDT,M10.1.0/2,M4.1.0/3", 600, 660, DstRuleKind::Australia},
    {kAuckland, "Auckland (NZ, DST)", "NZST-12NZDT,M9.5.0/2,M4.1.0/3", 720, 780, DstRuleKind::NewZealand},
};

struct DateTime
{
    int year = 1970;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

int days_in_month(int year, int month)
{
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && is_leap_year(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::time_t utc_epoch_from_local(int year, int month, int day, int hour, int minute, int second, int offset_min)
{
    const int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const int64_t local_seconds = days * 86400 + static_cast<int64_t>(hour) * 3600 +
                                  static_cast<int64_t>(minute) * 60 + second;
    return static_cast<std::time_t>(local_seconds - static_cast<int64_t>(offset_min) * 60);
}

bool utc_to_datetime(std::time_t epoch, DateTime& out)
{
    std::tm tm_utc{};
#if defined(_MSC_VER)
    if (gmtime_s(&tm_utc, &epoch) != 0)
    {
        return false;
    }
#elif defined(_WIN32)
    std::tm* tm_ptr = std::gmtime(&epoch);
    if (tm_ptr == nullptr)
    {
        return false;
    }
    tm_utc = *tm_ptr;
#else
    if (gmtime_r(&epoch, &tm_utc) == nullptr)
    {
        return false;
    }
#endif
    out.year = tm_utc.tm_year + 1900;
    out.month = tm_utc.tm_mon + 1;
    out.day = tm_utc.tm_mday;
    out.hour = tm_utc.tm_hour;
    out.minute = tm_utc.tm_min;
    out.second = tm_utc.tm_sec;
    return true;
}

int weekday_utc(int year, int month, int day)
{
    const int64_t days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int weekday = static_cast<int>((days + 4) % 7);
    if (weekday < 0)
    {
        weekday += 7;
    }
    return weekday;
}

int nth_weekday_day(int year, int month, int weekday, int nth)
{
    const int first_weekday = weekday_utc(year, month, 1);
    const int first_matching_day = 1 + ((weekday - first_weekday + 7) % 7);
    if (nth >= 5)
    {
        int day = first_matching_day;
        const int max_day = days_in_month(year, month);
        while (day + 7 <= max_day)
        {
            day += 7;
        }
        return day;
    }
    return first_matching_day + (nth - 1) * 7;
}

bool in_simple_dst_window(std::time_t utc_seconds,
                          int year,
                          int start_month,
                          int start_nth,
                          int start_weekday,
                          int start_hour,
                          int start_offset_min,
                          int end_month,
                          int end_nth,
                          int end_weekday,
                          int end_hour,
                          int end_offset_min)
{
    const int start_day = nth_weekday_day(year, start_month, start_weekday, start_nth);
    const int end_day = nth_weekday_day(year, end_month, end_weekday, end_nth);
    const std::time_t start_utc =
        utc_epoch_from_local(year, start_month, start_day, start_hour, 0, 0, start_offset_min);
    const std::time_t end_utc =
        utc_epoch_from_local(year, end_month, end_day, end_hour, 0, 0, end_offset_min);

    if (start_utc <= end_utc)
    {
        return utc_seconds >= start_utc && utc_seconds < end_utc;
    }
    return utc_seconds >= start_utc || utc_seconds < end_utc;
}

bool is_dst_for_rule(const TimezoneProfile& profile, std::time_t utc_seconds)
{
    if (utc_seconds <= 0 || profile.dst_rule == DstRuleKind::None)
    {
        return false;
    }

    DateTime utc{};
    if (!utc_to_datetime(utc_seconds, utc))
    {
        return false;
    }

    switch (profile.dst_rule)
    {
    case DstRuleKind::Us:
        return in_simple_dst_window(utc_seconds,
                                    utc.year,
                                    3,
                                    2,
                                    0,
                                    2,
                                    profile.standard_offset_min,
                                    11,
                                    1,
                                    0,
                                    2,
                                    profile.daylight_offset_min);
    case DstRuleKind::Eu:
        return in_simple_dst_window(utc_seconds, utc.year, 3, 5, 0, 1, 0, 10, 5, 0, 1, 0);
    case DstRuleKind::Australia:
    {
        const int season_year = utc.month < 7 ? utc.year - 1 : utc.year;
        return in_simple_dst_window(utc_seconds, season_year, 10, 1, 0, 2, 600, 4, 1, 0, 3, 660);
    }
    case DstRuleKind::NewZealand:
    {
        const int season_year = utc.month < 7 ? utc.year - 1 : utc.year;
        return in_simple_dst_window(utc_seconds, season_year, 9, 5, 0, 2, 720, 4, 1, 0, 3, 780);
    }
    case DstRuleKind::None:
    default:
        return false;
    }
}

bool same_text(const char* lhs, const char* rhs)
{
    return lhs && rhs && std::strcmp(lhs, rhs) == 0;
}

} // namespace

const TimezoneProfile* timezone_profiles(std::size_t* out_count)
{
    if (out_count)
    {
        *out_count = sizeof(kProfiles) / sizeof(kProfiles[0]);
    }
    return kProfiles;
}

const TimezoneProfile* default_timezone_profile()
{
    return &kProfiles[0];
}

const TimezoneProfile* timezone_profile_by_id(int id)
{
    for (const auto& profile : kProfiles)
    {
        if (profile.id == id)
        {
            return &profile;
        }
    }
    return nullptr;
}

const TimezoneProfile* timezone_profile_by_tzdef(const char* tzdef)
{
    if (!tzdef || tzdef[0] == '\0')
    {
        return nullptr;
    }
    for (const auto& profile : kProfiles)
    {
        if (same_text(profile.tzdef, tzdef))
        {
            return &profile;
        }
    }
    return nullptr;
}

const TimezoneProfile* timezone_profile_by_offset(int offset_min)
{
    for (const auto& profile : kProfiles)
    {
        if (profile.dst_rule == DstRuleKind::None && profile.standard_offset_min == offset_min)
        {
            return &profile;
        }
    }
    return nullptr;
}

int timezone_offset_for_profile_at(const TimezoneProfile& profile, std::time_t utc_seconds)
{
    if (is_dst_for_rule(profile, utc_seconds))
    {
        return profile.daylight_offset_min;
    }
    return profile.standard_offset_min;
}

int timezone_offset_for_profile_id_at(int profile_id, std::time_t utc_seconds)
{
    const TimezoneProfile* profile = timezone_profile_by_id(profile_id);
    return timezone_offset_for_profile_at(profile ? *profile : *default_timezone_profile(), utc_seconds);
}

bool timezone_profile_id_is_fixed(int profile_id)
{
    return profile_id == kFixedTimezoneProfileId;
}

int timezone_offset_for_profile_id_at(int profile_id, int fixed_offset_min, std::time_t utc_seconds)
{
    if (timezone_profile_id_is_fixed(profile_id))
    {
        return fixed_offset_min;
    }
    return timezone_offset_for_profile_id_at(profile_id, utc_seconds);
}

int timezone_profile_id_for_offset(int offset_min)
{
    const TimezoneProfile* profile = timezone_profile_by_offset(offset_min);
    return profile ? profile->id : default_timezone_profile()->id;
}

int timezone_profile_id_for_legacy_offset(int offset_min)
{
    switch (offset_min)
    {
    case 60:
        return kBerlin;
    case -300:
        return kUsEastern;
    case -360:
        return kUsCentral;
    case -420:
        return kUsMountain;
    case -480:
        return kUsPacific;
    case 600:
        return kSydney;
    case 720:
        return kAuckland;
    default:
        return timezone_profile_id_for_offset(offset_min);
    }
}

int timezone_profile_id_for_fixed_offset(int offset_min)
{
    const TimezoneProfile* profile = timezone_profile_by_offset(offset_min);
    return profile ? profile->id : kFixedTimezoneProfileId;
}

bool parse_posix_tz_standard_offset_minutes(const char* tzdef, int* out_offset_min)
{
    if (!tzdef || !out_offset_min || tzdef[0] == '\0')
    {
        return false;
    }

    const char* cursor = tzdef;
    if (*cursor == '<')
    {
        ++cursor;
        while (*cursor != '\0' && *cursor != '>')
        {
            ++cursor;
        }
        if (*cursor != '>')
        {
            return false;
        }
        ++cursor;
    }
    else
    {
        std::size_t name_len = 0;
        while (*cursor != '\0' && std::isalpha(static_cast<unsigned char>(*cursor)))
        {
            ++cursor;
            ++name_len;
        }
        if (name_len < 3U)
        {
            return false;
        }
    }

    int sign = 1;
    if (*cursor == '-')
    {
        sign = -1;
        ++cursor;
    }
    else if (*cursor == '+')
    {
        ++cursor;
    }

    if (!std::isdigit(static_cast<unsigned char>(*cursor)))
    {
        return false;
    }

    int hours = 0;
    while (std::isdigit(static_cast<unsigned char>(*cursor)))
    {
        hours = (hours * 10) + (*cursor - '0');
        ++cursor;
    }

    int minutes = 0;
    if (*cursor == ':')
    {
        ++cursor;
        if (!std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            return false;
        }
        while (std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            minutes = (minutes * 10) + (*cursor - '0');
            ++cursor;
        }
        if (*cursor == ':')
        {
            ++cursor;
            while (std::isdigit(static_cast<unsigned char>(*cursor)))
            {
                ++cursor;
            }
        }
    }

    const int posix_offset_min = sign * ((hours * 60) + minutes);
    *out_offset_min = -posix_offset_min;
    return true;
}

void build_fixed_posix_tzdef(int offset_min, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int posix_offset_min = -offset_min;
    const int abs_minutes = posix_offset_min < 0 ? -posix_offset_min : posix_offset_min;
    const int abs_hours = abs_minutes / 60;
    const int rem_minutes = abs_minutes % 60;

    if (rem_minutes == 0)
    {
        std::snprintf(out, out_len, "UTC%+d", posix_offset_min / 60);
    }
    else
    {
        std::snprintf(out,
                      out_len,
                      "UTC%+d:%02d",
                      posix_offset_min < 0 ? -abs_hours : abs_hours,
                      rem_minutes);
    }
}

} // namespace platform::ui::time
