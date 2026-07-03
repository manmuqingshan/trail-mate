#include "boards/t_echo_lite/gps_runtime.h"

#include "boards/t_echo_lite/board_profile.h"
#include "boards/t_echo_lite/t_echo_lite_board.h"

#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace boards::t_echo_lite
{
namespace
{

constexpr uint32_t kMinValidEpochSeconds = 1700000000UL;
constexpr std::size_t kNmeaFieldMax = 24;
constexpr std::size_t kGpsTailCanarySize = 32;
constexpr uint8_t kGpsTailCanaryByte = 0xA5;
constexpr uint32_t kTimeSyncHeartbeatLogMs = 60000UL;
constexpr uint32_t kTimeSyncJumpThresholdS = 5UL;
constexpr bool kGpsFlowDebugLog = false;
constexpr uint8_t kGpsPowerStrategyOff = 2;
constexpr uint32_t kMotionProbeRetryMs = 30000UL;
constexpr uint32_t kMotionStatusLogMs = 10000UL;
constexpr uint32_t kMotionPollFloorMs = 200UL;
constexpr uint32_t kMotionPollDefaultMs = 1000UL;
constexpr uint8_t kIcm20948WhoAmI = 0xEA;
constexpr uint8_t kIcmBankSelectReg = 0x7F;
constexpr uint8_t kIcmWhoAmIReg = 0x00;
constexpr uint8_t kIcmPwrMgmt1Reg = 0x06;
constexpr uint8_t kIcmPwrMgmt2Reg = 0x07;
constexpr uint8_t kIcmAccelXoutHReg = 0x2D;

struct AccelSample
{
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
};

int32_t abs32(int32_t value)
{
    return value < 0 ? -value : value;
}

enum class TimeSyncSource : uint8_t
{
    None = 0,
    Gnss,
    External,
};

uint32_t readSystemEpochSeconds()
{
    const time_t now = ::time(nullptr);
    if (now < static_cast<time_t>(kMinValidEpochSeconds))
    {
        return 0;
    }
    return static_cast<uint32_t>(now);
}

void syncSystemClockFromEpoch(uint32_t epoch_s)
{
    (void)epoch_s;
}

void fillCanary(uint8_t* bytes, std::size_t len)
{
    if (!bytes || len == 0)
    {
        return;
    }
    std::memset(bytes, static_cast<int>(kGpsTailCanaryByte), len);
}

bool canaryIntact(const uint8_t* bytes, std::size_t len)
{
    if (!bytes)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (bytes[i] != kGpsTailCanaryByte)
        {
            return false;
        }
    }
    return true;
}

int firstBadCanaryOffset(const uint8_t* bytes, std::size_t len)
{
    if (!bytes)
    {
        return -1;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (bytes[i] != kGpsTailCanaryByte)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void dumpCanaryHex(const uint8_t* bytes, std::size_t len, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!bytes || len == 0)
    {
        return;
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::size_t pos = 0;
    for (std::size_t i = 0; i < len && (pos + 3) < out_len; ++i)
    {
        const uint8_t b = bytes[i];
        out[pos++] = kHex[(b >> 4) & 0x0F];
        out[pos++] = kHex[b & 0x0F];
        if (i + 1 < len)
        {
            out[pos++] = ' ';
        }
    }
    out[pos] = '\0';
}

enum class CollectorSlot : uint8_t
{
    GPS = 0,
    GLN = 1,
    GAL = 2,
    BD = 3,
    UNKNOWN = 4,
};

CollectorSlot collectorSlotForTalker(const char* talker)
{
    if (!talker || talker[0] == '\0' || talker[1] == '\0')
    {
        return CollectorSlot::UNKNOWN;
    }
    if (talker[0] == 'G' && talker[1] == 'P')
    {
        return CollectorSlot::GPS;
    }
    if (talker[0] == 'G' && talker[1] == 'L')
    {
        return CollectorSlot::GLN;
    }
    if (talker[0] == 'G' && talker[1] == 'A')
    {
        return CollectorSlot::GAL;
    }
    if ((talker[0] == 'G' && talker[1] == 'B') || (talker[0] == 'B' && talker[1] == 'D'))
    {
        return CollectorSlot::BD;
    }
    return CollectorSlot::UNKNOWN;
}

::gps::GnssSystem systemForSlot(CollectorSlot slot)
{
    switch (slot)
    {
    case CollectorSlot::GPS:
        return ::gps::GnssSystem::GPS;
    case CollectorSlot::GLN:
        return ::gps::GnssSystem::GLN;
    case CollectorSlot::GAL:
        return ::gps::GnssSystem::GAL;
    case CollectorSlot::BD:
        return ::gps::GnssSystem::BD;
    default:
        return ::gps::GnssSystem::UNKNOWN;
    }
}

bool parseUint(const char* text, uint32_t* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    unsigned long value = std::strtoul(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseInt(const char* text, int* out)
{
    if (!text || !*text || !out)
    {
        return false;
    }
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (end == text)
    {
        return false;
    }
    *out = static_cast<int>(value);
    return true;
}

bool verifyChecksum(const char* line)
{
    if (!line || line[0] != '$')
    {
        return false;
    }

    const char* star = std::strchr(line, '*');
    if (!star || !star[1] || !star[2])
    {
        return true;
    }

    uint8_t checksum = 0;
    for (const char* cursor = line + 1; cursor < star; ++cursor)
    {
        checksum ^= static_cast<uint8_t>(*cursor);
    }

    char checksum_text[3] = {star[1], star[2], '\0'};
    char* end = nullptr;
    long expected = std::strtol(checksum_text, &end, 16);
    return end != checksum_text && checksum == static_cast<uint8_t>(expected & 0xFF);
}

std::size_t splitFields(char* sentence, std::array<char*, kNmeaFieldMax>& fields)
{
    fields.fill(nullptr);
    std::size_t count = 0;
    char* cursor = sentence;
    while (cursor && *cursor && count < fields.size())
    {
        fields[count++] = cursor;
        char* comma = std::strchr(cursor, ',');
        if (!comma)
        {
            break;
        }
        *comma = '\0';
        cursor = comma + 1;
    }
    return count;
}

uint8_t daysInMonth(int year, uint8_t month)
{
    static constexpr uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 0 || month > 12)
    {
        return 31;
    }
    if (month != 2)
    {
        return kDays[month - 1];
    }
    const bool leap = ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
    return leap ? 29 : 28;
}

bool gpsDateTimeValid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    if (year < 2020 || year > 2100)
    {
        return false;
    }
    if (month < 1 || month > 12)
    {
        return false;
    }
    const uint8_t max_day = daysInMonth(year, month);
    if (day < 1 || day > max_day)
    {
        return false;
    }
    if (hour >= 24 || minute >= 60 || second >= 60)
    {
        return false;
    }
    return true;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsDateTimeToEpochUtc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = daysFromCivil(year, month, day);
    const int64_t sec_of_day =
        static_cast<int64_t>(hour) * 3600 + static_cast<int64_t>(minute) * 60 + static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

void beginGpsSerial()
{
    const auto& profile = kBoardProfile;
    if (profile.gps.uart.aux >= 0)
    {
        pinMode(profile.gps.uart.aux, OUTPUT);
        digitalWrite(profile.gps.uart.aux, HIGH);
    }
    Serial1.setPins(profile.gps.uart.rx, profile.gps.uart.tx);
    Serial1.begin(profile.gps.baud_rate);
}

void endGpsSerial()
{
    const auto& profile = kBoardProfile;
    Serial1.end();
    if (profile.gps.uart.aux >= 0)
    {
        digitalWrite(profile.gps.uart.aux, LOW);
    }
}

bool appendI2cAddress(char* out, std::size_t out_len, uint8_t address)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    const std::size_t used = std::strlen(out);
    if (used + 6 >= out_len)
    {
        return false;
    }
    std::snprintf(out + used, out_len - used, "%s0x%02X", used > 0 ? "," : "", static_cast<unsigned>(address));
    return true;
}

bool i2cAddressResponds(TwoWire& wire, uint8_t address)
{
    wire.beginTransmission(address);
    return wire.endTransmission() == 0;
}

void scanI2cBusForLog(TwoWire& wire, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    for (uint8_t address = 1; address < 0x7F; ++address)
    {
        if (i2cAddressResponds(wire, address))
        {
            (void)appendI2cAddress(out, out_len, address);
        }
    }
    if (out[0] == '\0')
    {
        std::snprintf(out, out_len, "none");
    }
}

bool i2cWriteByte(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t value)
{
    wire.beginTransmission(address);
    wire.write(reg);
    wire.write(value);
    return wire.endTransmission() == 0;
}

bool i2cReadBytes(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t* out, std::size_t len)
{
    if (!out || len == 0 || len > 32)
    {
        return false;
    }

    wire.beginTransmission(address);
    wire.write(reg);
    if (wire.endTransmission(false) != 0)
    {
        return false;
    }

    const uint8_t requested = static_cast<uint8_t>(len);
    const uint8_t received = wire.requestFrom(address, requested);
    if (received != requested)
    {
        return false;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        out[index] = static_cast<uint8_t>(wire.read());
    }
    return true;
}

bool icmSelectBank(TwoWire& wire, uint8_t address, uint8_t bank)
{
    return i2cWriteByte(wire, address, kIcmBankSelectReg, static_cast<uint8_t>((bank & 0x03) << 4));
}

bool icmReadByte(TwoWire& wire, uint8_t address, uint8_t bank, uint8_t reg, uint8_t* out)
{
    return icmSelectBank(wire, address, bank) && i2cReadBytes(wire, address, reg, out, 1);
}

bool icmWriteByte(TwoWire& wire, uint8_t address, uint8_t bank, uint8_t reg, uint8_t value)
{
    return icmSelectBank(wire, address, bank) && i2cWriteByte(wire, address, reg, value);
}

bool icmReadAccel(TwoWire& wire, uint8_t address, AccelSample* out)
{
    if (!out || !icmSelectBank(wire, address, 0))
    {
        return false;
    }

    uint8_t raw[6] = {};
    if (!i2cReadBytes(wire, address, kIcmAccelXoutHReg, raw, sizeof(raw)))
    {
        return false;
    }

    out->x = static_cast<int16_t>((static_cast<uint16_t>(raw[0]) << 8) | raw[1]);
    out->y = static_cast<int16_t>((static_cast<uint16_t>(raw[2]) << 8) | raw[3]);
    out->z = static_cast<int16_t>((static_cast<uint16_t>(raw[4]) << 8) | raw[5]);
    return true;
}

} // namespace

struct GpsRuntime::Impl
{
    static constexpr uint32_t kGuardValue = 0x47505352UL;

    struct GsvCollector
    {
        std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
        std::size_t count = 0;
    };

    uint32_t guard_begin = kGuardValue;
    TinyGPSPlus parser{};
    ::gps::GpsState data{};
    ::gps::GnssStatus status{};
    uint32_t last_motion_ms = 0;
    uint32_t collection_interval_ms = 60000;
    uint32_t motion_idle_timeout_ms = 0;
    uint8_t power_strategy = 0;
    uint8_t gnss_mode = 0;
    uint8_t sat_mask = 0;
    uint8_t external_nmea_output_hz = 0;
    uint8_t external_nmea_sentence_mask = 0;
    uint8_t motion_sensor_id = 0;
    uint32_t motion_poll_interval_ms = kMotionPollDefaultMs;
    uint32_t last_motion_poll_ms = 0;
    uint32_t last_motion_probe_ms = 0;
    uint32_t last_motion_status_log_ms = 0;
    uint8_t motion_address = 0;
    uint8_t motion_read_fail_count = 0;
    bool motion_probe_done = false;
    bool motion_i2c_scan_logged = false;
    bool motion_available = false;
    bool motion_sample_valid = false;
    AccelSample last_motion_sample{};
    uint32_t epoch_base_s = 0;
    uint32_t epoch_base_ms = 0;
    uint32_t last_nmea_ms = 0;
    uint32_t last_time_sync_log_ms = 0;
    uint32_t last_time_sync_epoch_logged = 0;
    TimeSyncSource last_time_sync_source = TimeSyncSource::None;
    uint32_t last_status_log_ms = 0;
    std::array<GsvCollector, 5> gsv{};
    std::array<::gps::GnssSatInfo, ::gps::kMaxGnssSats> sats{};
    std::array<uint16_t, ::gps::kMaxGnssSats> used_sat_ids{};
    char nmea_line[96] = {};
    std::size_t sat_count = 0;
    std::size_t used_sat_count = 0;
    std::size_t nmea_line_len = 0;
    bool user_enabled = true;
    bool enabled = true;
    bool powered = false;
    bool initialized = false;
    bool time_synced = false;
    bool nmea_seen = false;
    uint32_t last_sat_diag_log_ms = 0;
    uint8_t last_logged_parser_sats = 0xFF;
    uint8_t last_logged_status_in_view = 0xFF;
    uint8_t last_logged_status_in_use = 0xFF;
    std::size_t last_logged_snapshot_count = static_cast<std::size_t>(-1);
    bool last_logged_snapshot_available = false;
    bool first_nmea_sentence_logged = false;
    std::array<uint8_t, kGpsTailCanarySize> tail_canary{};
    uint32_t guard_end = kGuardValue;

    void initializeDebugGuards()
    {
        guard_begin = kGuardValue;
        guard_end = kGuardValue;
        fillCanary(tail_canary.data(), tail_canary.size());
    }

    void logMemoryLayout(const char* reason) const
    {
        Serial.printf(
            "[T-Echo Lite][gps][mem] reason=%s impl=%p size=%u guard_begin@%p guard_end@%p canary@%p canary_len=%u parser@%p data@%p status@%p sats@%p used_ids@%p nmea@%p\n",
            reason ? reason : "unknown",
            static_cast<const void*>(this),
            static_cast<unsigned>(sizeof(*this)),
            static_cast<const void*>(&guard_begin),
            static_cast<const void*>(&guard_end),
            static_cast<const void*>(tail_canary.data()),
            static_cast<unsigned>(tail_canary.size()),
            static_cast<const void*>(&parser),
            static_cast<const void*>(&data),
            static_cast<const void*>(&status),
            static_cast<const void*>(sats.data()),
            static_cast<const void*>(used_sat_ids.data()),
            static_cast<const void*>(nmea_line));
    }

    bool usedSat(uint16_t sat_id) const
    {
        for (std::size_t i = 0; i < used_sat_count; ++i)
        {
            if (used_sat_ids[i] == sat_id)
            {
                return true;
            }
        }
        return false;
    }

    void mergeGnssSatellites()
    {
        sat_count = 0;
        for (std::size_t collector_index = 0; collector_index < gsv.size(); ++collector_index)
        {
            auto& collector = gsv[collector_index];
            for (std::size_t sat_index = 0; sat_index < collector.count && sat_count < sats.size(); ++sat_index)
            {
                auto sat = collector.sats[sat_index];
                sat.used = usedSat(sat.id);
                sats[sat_count++] = sat;
            }
        }

        status.sats_in_view = static_cast<uint8_t>(std::min<std::size_t>(sat_count, 255U));
        if (used_sat_count > 0)
        {
            status.sats_in_use = static_cast<uint8_t>(std::min<std::size_t>(used_sat_count, 255U));
        }
    }

    void clearObservations()
    {
        parser = TinyGPSPlus{};
        data = ::gps::GpsState{};
        status = ::gps::GnssStatus{};
        sats.fill(::gps::GnssSatInfo{});
        used_sat_ids.fill(0);
        gsv.fill(GsvCollector{});
        sat_count = 0;
        used_sat_count = 0;
        last_nmea_ms = 0;
        nmea_line_len = 0;
        nmea_line[0] = '\0';
        nmea_seen = false;
    }

    uint32_t effectiveMotionPollIntervalMs() const
    {
        uint32_t interval = motion_poll_interval_ms > 0 ? motion_poll_interval_ms : kMotionPollDefaultMs;
        if (interval < kMotionPollFloorMs)
        {
            interval = kMotionPollFloorMs;
        }
        return interval;
    }

    bool motionGateEnabled() const
    {
        return motion_available && motion_idle_timeout_ms > 0 && power_strategy != kGpsPowerStrategyOff;
    }

    bool shouldPowerGps(uint32_t now_ms) const
    {
        if (!enabled)
        {
            return false;
        }
        if (power_strategy == kGpsPowerStrategyOff)
        {
            return false;
        }
        if (!motionGateEnabled())
        {
            return true;
        }
        if (last_motion_ms == 0)
        {
            return false;
        }
        return (now_ms - last_motion_ms) < motion_idle_timeout_ms;
    }

    void setGpsPower(bool on, const char* reason)
    {
        if (on)
        {
            if (powered)
            {
                return;
            }
            beginGpsSerial();
            powered = true;
            Serial.printf("[T-Echo Lite][gps] power on reason=%s motion=%u last_motion_ms=%lu\n",
                          reason ? reason : "unknown",
                          static_cast<unsigned>(motion_available ? 1 : 0),
                          static_cast<unsigned long>(last_motion_ms));
            return;
        }

        if (!powered)
        {
            return;
        }
        endGpsSerial();
        powered = false;
        clearObservations();
        Serial.printf("[T-Echo Lite][gps] power off reason=%s motion=%u last_motion_ms=%lu\n",
                      reason ? reason : "unknown",
                      static_cast<unsigned>(motion_available ? 1 : 0),
                      static_cast<unsigned long>(last_motion_ms));
    }

    void logMotionStatusIfDue(uint32_t now_ms, const char* reason)
    {
        if (last_motion_status_log_ms != 0 && (now_ms - last_motion_status_log_ms) < kMotionStatusLogMs)
        {
            return;
        }
        last_motion_status_log_ms = now_ms;

        const uint32_t motion_age_ms =
            last_motion_ms > 0 ? (now_ms - last_motion_ms) : 0xFFFFFFFFUL;
        Serial.printf(
            "[T-Echo Lite][motion] status reason=%s available=%u addr=0x%02X powered=%u gate=%u last_motion_age_ms=%lu idle_ms=%lu sample=%u\n",
            reason ? reason : "tick",
            static_cast<unsigned>(motion_available ? 1 : 0),
            static_cast<unsigned>(motion_address),
            static_cast<unsigned>(powered ? 1 : 0),
            static_cast<unsigned>(motionGateEnabled() ? 1 : 0),
            static_cast<unsigned long>(motion_age_ms),
            static_cast<unsigned long>(motion_idle_timeout_ms),
            static_cast<unsigned>(motion_sample_valid ? 1 : 0));
    }

    void updateGpsPowerState(uint32_t now_ms, const char* reason)
    {
        const bool should_power = shouldPowerGps(now_ms);
        if (should_power != powered)
        {
            setGpsPower(should_power, reason);
        }
        if (!should_power || motionGateEnabled())
        {
            logMotionStatusIfDue(now_ms, reason);
        }
    }

    bool readMotionSample(AccelSample* out)
    {
        if (!out || !motion_available || motion_address == 0)
        {
            return false;
        }

        auto& board = ::boards::t_echo_lite::TEchoLiteBoard::instance();
        if (!board.ensureI2cReady())
        {
            return false;
        }
        ::boards::t_echo_lite::TEchoLiteBoard::I2cGuard guard(board, 30);
        if (!guard)
        {
            return false;
        }
        return icmReadAccel(board.i2cWire(), motion_address, out);
    }

    bool probeMotionSensor(uint32_t now_ms, bool force_log)
    {
        if (motion_available)
        {
            return true;
        }
        if (motion_probe_done && !force_log && (now_ms - last_motion_probe_ms) < kMotionProbeRetryMs)
        {
            return false;
        }

        motion_probe_done = true;
        last_motion_probe_ms = now_ms;

        auto& board = ::boards::t_echo_lite::TEchoLiteBoard::instance();
        if (!board.ensureI2cReady())
        {
            if (force_log)
            {
                Serial.printf("[T-Echo Lite][motion] i2c not ready for optional ICM20948 probe\n");
            }
            return false;
        }

        ::boards::t_echo_lite::TEchoLiteBoard::I2cGuard guard(board, 80);
        if (!guard)
        {
            if (force_log)
            {
                Serial.printf("[T-Echo Lite][motion] i2c lock failed for optional ICM20948 probe\n");
            }
            return false;
        }

        auto& wire = board.i2cWire();
        if (!motion_i2c_scan_logged || force_log)
        {
            char devices[128] = {};
            scanI2cBusForLog(wire, devices, sizeof(devices));
            Serial.printf("[T-Echo Lite][motion] i2c scan devices=%s\n", devices);
            motion_i2c_scan_logged = true;
        }

        const auto& motion = kBoardProfile.motion;
        const uint8_t addresses[2] = {motion.primary_address, motion.secondary_address};
        for (uint8_t address : addresses)
        {
            if (address == 0 || !i2cAddressResponds(wire, address))
            {
                continue;
            }

            uint8_t who = 0;
            if (!icmReadByte(wire, address, 0, kIcmWhoAmIReg, &who))
            {
                Serial.printf("[T-Echo Lite][motion] candidate addr=0x%02X who read failed\n",
                              static_cast<unsigned>(address));
                continue;
            }
            if (who != kIcm20948WhoAmI)
            {
                Serial.printf("[T-Echo Lite][motion] candidate addr=0x%02X unexpected who=0x%02X\n",
                              static_cast<unsigned>(address),
                              static_cast<unsigned>(who));
                continue;
            }

            (void)icmWriteByte(wire, address, 0, kIcmPwrMgmt1Reg, 0x01);
            (void)icmWriteByte(wire, address, 0, kIcmPwrMgmt2Reg, 0x00);
            delay(10);

            motion_address = address;
            motion_available = true;
            motion_sample_valid = false;
            motion_read_fail_count = 0;
            last_motion_ms = 0;
            last_motion_poll_ms = 0;
            Serial.printf(
                "[T-Echo Lite][motion] ICM20948 detected addr=0x%02X who=0x%02X int_pin=%d threshold=%u\n",
                static_cast<unsigned>(motion_address),
                static_cast<unsigned>(who),
                motion.interrupt_pin,
                static_cast<unsigned>(motion.accel_delta_threshold));
            return true;
        }

        motion_address = 0;
        motion_available = false;
        motion_sample_valid = false;
        if (force_log)
        {
            Serial.printf("[T-Echo Lite][motion] optional ICM20948 not detected; GPS keeps normal power behavior\n");
        }
        return false;
    }

    void pollMotionSensor(uint32_t now_ms)
    {
        if (!enabled || power_strategy == kGpsPowerStrategyOff || motion_idle_timeout_ms == 0)
        {
            return;
        }

        if (!motion_available && !probeMotionSensor(now_ms, false))
        {
            return;
        }

        if (last_motion_poll_ms != 0 && (now_ms - last_motion_poll_ms) < effectiveMotionPollIntervalMs())
        {
            return;
        }
        last_motion_poll_ms = now_ms;

        AccelSample sample{};
        if (!readMotionSample(&sample))
        {
            if (motion_read_fail_count < 255)
            {
                ++motion_read_fail_count;
            }
            if (motion_read_fail_count >= 5)
            {
                Serial.printf("[T-Echo Lite][motion] ICM20948 read failed repeatedly; disabling motion gate and using normal GPS power\n");
                motion_available = false;
                motion_probe_done = false;
                motion_address = 0;
                motion_sample_valid = false;
                updateGpsPowerState(now_ms, "motion-read-fail");
            }
            return;
        }

        motion_read_fail_count = 0;
        if (!motion_sample_valid)
        {
            last_motion_sample = sample;
            motion_sample_valid = true;
            Serial.printf("[T-Echo Lite][motion] baseline x=%d y=%d z=%d\n",
                          static_cast<int>(sample.x),
                          static_cast<int>(sample.y),
                          static_cast<int>(sample.z));
            return;
        }

        const int32_t delta = abs32(static_cast<int32_t>(sample.x) - last_motion_sample.x) +
                              abs32(static_cast<int32_t>(sample.y) - last_motion_sample.y) +
                              abs32(static_cast<int32_t>(sample.z) - last_motion_sample.z);
        last_motion_sample = sample;

        if (delta >= kBoardProfile.motion.accel_delta_threshold)
        {
            last_motion_ms = now_ms;
            Serial.printf("[T-Echo Lite][motion] movement delta=%ld x=%d y=%d z=%d\n",
                          static_cast<long>(delta),
                          static_cast<int>(sample.x),
                          static_cast<int>(sample.y),
                          static_cast<int>(sample.z));
        }
    }

    bool satelliteStateLooksCorrupt() const
    {
        if (guard_begin != kGuardValue || guard_end != kGuardValue)
        {
            return true;
        }
        if (!canaryIntact(tail_canary.data(), tail_canary.size()))
        {
            return true;
        }
        if (sat_count > sats.size())
        {
            return true;
        }
        if (used_sat_count > used_sat_ids.size())
        {
            return true;
        }
        if (status.sats_in_view > ::gps::kMaxGnssSats)
        {
            return true;
        }
        if (status.sats_in_use > ::gps::kMaxGnssSats)
        {
            return true;
        }
        if (!parser.satellites.isValid() && data.satellites == 0 && sat_count == ::gps::kMaxGnssSats)
        {
            return true;
        }
        return false;
    }

    void repairCorruptSatelliteState(const char* reason)
    {
        const int bad_offset = firstBadCanaryOffset(tail_canary.data(), tail_canary.size());
        const uint8_t bad_value =
            (bad_offset >= 0) ? tail_canary[static_cast<std::size_t>(bad_offset)] : 0;

        char canary_hex[3 * kGpsTailCanarySize + 1] = {};
        dumpCanaryHex(tail_canary.data(), tail_canary.size(), canary_hex, sizeof(canary_hex));

        Serial.printf(
            "[T-Echo Lite][gps][corrupt] reason=%s impl=%p parser_sats=%u snapshot_count=%u used_count=%u status_view=%u status_use=%u guards=%08lX/%08lX canary_bad=%d bad_val=%02X canary=[%s]\n",
            reason ? reason : "unknown",
            static_cast<void*>(this),
            static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U),
            static_cast<unsigned>(sat_count),
            static_cast<unsigned>(used_sat_count),
            static_cast<unsigned>(status.sats_in_view),
            static_cast<unsigned>(status.sats_in_use),
            static_cast<unsigned long>(guard_begin),
            static_cast<unsigned long>(guard_end),
            bad_offset,
            static_cast<unsigned>(bad_value),
            canary_hex);

        logMemoryLayout("corrupt");

        guard_begin = kGuardValue;
        guard_end = kGuardValue;
        fillCanary(tail_canary.data(), tail_canary.size());

        sats.fill(::gps::GnssSatInfo{});
        used_sat_ids.fill(0);
        gsv.fill(GsvCollector{});
        sat_count = 0;
        used_sat_count = 0;
        status.sats_in_view = 0;
        status.sats_in_use = 0;
        data.satellites = parser.satellites.isValid()
                              ? static_cast<uint8_t>(std::min<uint32_t>(parser.satellites.value(), 255U))
                              : 0U;
    }

    void parseGsaSentence(const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
    {
        if (count < 4)
        {
            return;
        }

        used_sat_count = 0;
        for (std::size_t i = 3; i <= 14 && i < count; ++i)
        {
            uint32_t sat_id = 0;
            if (!parseUint(fields[i], &sat_id) || sat_id == 0 || used_sat_count >= used_sat_ids.size())
            {
                continue;
            }
            used_sat_ids[used_sat_count++] = static_cast<uint16_t>(sat_id);
        }
        mergeGnssSatellites();
        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("parse_gsa");
        }
    }

    void parseGsvSentence(const char* talker, const std::array<char*, kNmeaFieldMax>& fields, std::size_t count)
    {
        if (count < 4)
        {
            return;
        }

        const CollectorSlot slot = collectorSlotForTalker(talker);
        auto& collector = gsv[static_cast<std::size_t>(slot)];

        uint32_t msg_num = 0;
        if (!parseUint(fields[2], &msg_num))
        {
            return;
        }

        uint32_t sats_in_view = 0;
        if (parseUint(fields[3], &sats_in_view))
        {
            status.sats_in_view = static_cast<uint8_t>(std::min<uint32_t>(sats_in_view, 255U));
        }

        if (msg_num == 1)
        {
            collector.count = 0;
        }

        for (std::size_t base = 4; base + 3 < count && collector.count < collector.sats.size(); base += 4)
        {
            uint32_t sat_id = 0;
            if (!parseUint(fields[base], &sat_id) || sat_id == 0)
            {
                continue;
            }

            ::gps::GnssSatInfo sat{};
            sat.id = static_cast<uint16_t>(sat_id);
            sat.sys = systemForSlot(slot);

            uint32_t elevation = 0;
            if (parseUint(fields[base + 1], &elevation))
            {
                sat.elevation = static_cast<uint8_t>(std::min<uint32_t>(elevation, 90U));
            }

            uint32_t azimuth = 0;
            if (parseUint(fields[base + 2], &azimuth))
            {
                sat.azimuth = static_cast<uint16_t>(std::min<uint32_t>(azimuth, 359U));
            }

            int snr = -1;
            if (parseInt(fields[base + 3], &snr))
            {
                sat.snr = static_cast<int8_t>(std::clamp(snr, -1, 99));
            }

            collector.sats[collector.count++] = sat;
        }

        mergeGnssSatellites();
        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("parse_gsv");
        }
    }

    void parseNmeaSentence(char* sentence)
    {
        if (!sentence || sentence[0] != '$' || !verifyChecksum(sentence))
        {
            return;
        }

        char* payload = sentence + 1;
        char* star = std::strchr(payload, '*');
        if (star)
        {
            *star = '\0';
        }

        std::array<char*, kNmeaFieldMax> fields{};
        const std::size_t count = splitFields(payload, fields);
        if (count == 0 || !fields[0] || std::strlen(fields[0]) < 5)
        {
            return;
        }

        char talker[3] = {fields[0][0], fields[0][1], '\0'};
        const char* type = fields[0] + std::strlen(fields[0]) - 3;

        if (kGpsFlowDebugLog && !first_nmea_sentence_logged)
        {
            first_nmea_sentence_logged = true;
            Serial.printf("[T-Echo Lite][gps][flow] first_nmea talker=%s type=%s raw_sats=%u\n",
                          talker,
                          type,
                          static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U));
        }

        if (std::strcmp(type, "GSA") == 0)
        {
            parseGsaSentence(fields, count);
        }
        else if (std::strcmp(type, "GSV") == 0)
        {
            parseGsvSentence(talker, fields, count);
        }
    }

    void processNmeaChar(char ch)
    {
        if (ch == '$')
        {
            nmea_line_len = 0;
            nmea_line[nmea_line_len++] = ch;
            return;
        }

        if (nmea_line_len == 0)
        {
            return;
        }

        if (ch == '\r' || ch == '\n')
        {
            if (nmea_line_len > 0)
            {
                nmea_line[nmea_line_len] = '\0';
                parseNmeaSentence(nmea_line);
                nmea_line_len = 0;
            }
            return;
        }

        if (nmea_line_len + 1 < sizeof(nmea_line))
        {
            nmea_line[nmea_line_len++] = ch;
        }
        else
        {
            nmea_line_len = 0;
        }
    }

    void applyTimeIfValid()
    {
        if (!parser.time.isValid() || !parser.date.isValid())
        {
            return;
        }

        const uint16_t year = parser.date.year();
        const uint8_t month = parser.date.month();
        const uint8_t day = parser.date.day();
        const uint8_t hour = parser.time.hour();
        const uint8_t minute = parser.time.minute();
        const uint8_t second = parser.time.second();

        if (!gpsDateTimeValid(year, month, day, hour, minute, second))
        {
            return;
        }

        const time_t utc = gpsDateTimeToEpochUtc(year, month, day, hour, minute, second);
        if (utc < static_cast<time_t>(kMinValidEpochSeconds))
        {
            return;
        }

        const uint32_t utc_s = static_cast<uint32_t>(utc);
        if (epoch_base_s == utc_s)
        {
            return;
        }

        const bool was_time_synced = time_synced;
        const uint32_t prev_epoch_s = epoch_base_s;
        const uint32_t prev_epoch_ms = epoch_base_ms;
        const uint32_t now_ms = millis();
        epoch_base_s = utc_s;
        epoch_base_ms = now_ms;
        time_synced = true;

        uint32_t expected_epoch_s = prev_epoch_s;
        if (prev_epoch_s != 0 && prev_epoch_ms != 0 && now_ms >= prev_epoch_ms)
        {
            expected_epoch_s += (now_ms - prev_epoch_ms) / 1000U;
        }
        const uint32_t jump_s =
            (prev_epoch_s == 0)
                ? 0
                : (utc_s >= expected_epoch_s ? (utc_s - expected_epoch_s) : (expected_epoch_s - utc_s));

        const bool first_sync = !was_time_synced || prev_epoch_s == 0;
        const bool source_changed = last_time_sync_source != TimeSyncSource::Gnss;
        const bool jump_detected = prev_epoch_s != 0 && jump_s >= kTimeSyncJumpThresholdS;
        const bool heartbeat_due =
            last_time_sync_log_ms == 0 || (now_ms - last_time_sync_log_ms) >= kTimeSyncHeartbeatLogMs;

        const char* reason = nullptr;
        if (first_sync)
        {
            reason = "first";
        }
        else if (source_changed)
        {
            reason = "source";
        }
        else if (jump_detected)
        {
            reason = "jump";
        }
        else if (heartbeat_due)
        {
            reason = "heartbeat";
        }

        last_time_sync_source = TimeSyncSource::Gnss;
        if (reason)
        {
            last_time_sync_epoch_logged = utc_s;
            last_time_sync_log_ms = now_ms;
            Serial.printf(
                "[T-Echo Lite][gps] time sync source=gnss reason=%s epoch=%lu prev=%lu jump_s=%lu sats=%u fix=%u age_ms=%lu date=%04u-%02u-%02u time=%02u:%02u:%02u\n",
                reason,
                static_cast<unsigned long>(utc_s),
                static_cast<unsigned long>(prev_epoch_s),
                static_cast<unsigned long>(jump_s),
                static_cast<unsigned>(parser.satellites.isValid() ? parser.satellites.value() : 0U),
                static_cast<unsigned>(parser.location.isValid() ? 1U : 0U),
                static_cast<unsigned long>(parser.location.isValid() ? parser.location.age() : 0U),
                static_cast<unsigned>(year),
                static_cast<unsigned>(month),
                static_cast<unsigned>(day),
                static_cast<unsigned>(hour),
                static_cast<unsigned>(minute),
                static_cast<unsigned>(second));
        }
        syncSystemClockFromEpoch(utc_s);
    }

    void logStatusIfDue()
    {
        if (!initialized || !enabled)
        {
            return;
        }

        const uint32_t now_ms = millis();
        const uint32_t interval_ms = collection_interval_ms > 0 ? collection_interval_ms : 60000U;
        if ((now_ms - last_status_log_ms) < interval_ms)
        {
            return;
        }
        last_status_log_ms = now_ms;

        const bool time_valid = parser.time.isValid();
        const bool date_valid = parser.date.isValid();
        const bool fix_valid = parser.location.isValid();
        const uint16_t year = date_valid ? parser.date.year() : 0U;
        const uint8_t month = date_valid ? parser.date.month() : 0U;
        const uint8_t day = date_valid ? parser.date.day() : 0U;
        const uint8_t hour = time_valid ? parser.time.hour() : 0U;
        const uint8_t minute = time_valid ? parser.time.minute() : 0U;
        const uint8_t second = time_valid ? parser.time.second() : 0U;
        const bool datetime_shape_valid =
            time_valid && date_valid && gpsDateTimeValid(year, month, day, hour, minute, second);
        const time_t utc = datetime_shape_valid ? gpsDateTimeToEpochUtc(year, month, day, hour, minute, second)
                                                : static_cast<time_t>(0);
        const bool epoch_ok = utc >= static_cast<time_t>(kMinValidEpochSeconds);
        const uint32_t sat_count_parser = parser.satellites.isValid() ? parser.satellites.value() : 0U;
        const uint32_t nmea_age_ms = last_nmea_ms > 0 ? (now_ms - last_nmea_ms) : 0U;
        const char* state = "idle";
        if (!powered)
        {
            if (power_strategy == kGpsPowerStrategyOff)
            {
                state = "strategy_off";
            }
            else if (motionGateEnabled() && last_motion_ms == 0)
            {
                state = "motion_wait";
            }
            else if (motionGateEnabled())
            {
                state = "motion_idle";
            }
            else
            {
                state = "power_off";
            }
        }
        else if (!nmea_seen)
        {
            state = "no_nmea";
        }
        else if (!time_valid || !date_valid)
        {
            state = "time_invalid";
        }
        else if (!datetime_shape_valid)
        {
            state = "datetime_reject";
        }
        else if (!epoch_ok)
        {
            state = "epoch_reject";
        }
        else if (sat_count_parser == 0U)
        {
            state = "time_only";
        }
        else if (!fix_valid)
        {
            state = "search_fix";
        }
        else if (!time_synced)
        {
            state = "ready_unsynced";
        }
        else
        {
            state = "synced";
        }

        Serial.printf(
            "[T-Echo Lite][gps] status state=%s enabled=%u powered=%u nmea=%u nmea_age_ms=%lu time=%u date=%u fix=%u sats=%u lat=%.6f lng=%.6f epoch=%lu utc=%lu dt=%04u-%02u-%02uT%02u:%02u:%02u\n",
            state,
            static_cast<unsigned>(enabled ? 1 : 0),
            static_cast<unsigned>(powered ? 1 : 0),
            static_cast<unsigned>(nmea_seen ? 1 : 0),
            static_cast<unsigned long>(nmea_age_ms),
            static_cast<unsigned>(time_valid ? 1 : 0),
            static_cast<unsigned>(date_valid ? 1 : 0),
            static_cast<unsigned>(fix_valid ? 1 : 0),
            static_cast<unsigned>(sat_count_parser),
            fix_valid ? parser.location.lat() : 0.0,
            fix_valid ? parser.location.lng() : 0.0,
            static_cast<unsigned long>(epoch_base_s),
            static_cast<unsigned long>(epoch_ok ? static_cast<uint32_t>(utc) : 0U),
            static_cast<unsigned>(year),
            static_cast<unsigned>(month),
            static_cast<unsigned>(day),
            static_cast<unsigned>(hour),
            static_cast<unsigned>(minute),
            static_cast<unsigned>(second));
    }

    void refreshFix()
    {
        data.valid = parser.location.isValid();
        data.lat = parser.location.lat();
        data.lng = parser.location.lng();
        data.has_alt = parser.altitude.isValid();
        data.alt_m = data.has_alt ? parser.altitude.meters() : 0.0;
        data.has_speed = parser.speed.isValid();
        data.speed_mps = data.has_speed ? (parser.speed.kmph() / 3.6) : 0.0;
        data.has_course = parser.course.isValid();
        data.course_deg = data.has_course ? parser.course.deg() : 0.0;
        data.satellites = static_cast<uint8_t>(
            std::min<uint32_t>(parser.satellites.isValid() ? parser.satellites.value() : 0U, 255U));
        data.age = parser.location.isValid() ? static_cast<uint32_t>(parser.location.age()) : 0xFFFFFFFFUL;

        status.sats_in_use = used_sat_count > 0
                                 ? static_cast<uint8_t>(std::min<std::size_t>(used_sat_count, 255U))
                                 : data.satellites;
        status.sats_in_view = sat_count > 0
                                  ? static_cast<uint8_t>(std::min<std::size_t>(sat_count, 255U))
                                  : data.satellites;

        if (sat_count == 0 && data.satellites == 0)
        {
            status.sats_in_use = 0;
            status.sats_in_view = 0;
        }

        status.hdop = parser.hdop.isValid() ? static_cast<float>(parser.hdop.hdop()) : 0.0f;
        status.fix = data.valid ? (data.has_alt ? ::gps::GnssFix::FIX3D : ::gps::GnssFix::FIX2D)
                                : ::gps::GnssFix::NOFIX;

        if (data.valid && !motion_available)
        {
            last_motion_ms = millis();
        }

        if (satelliteStateLooksCorrupt())
        {
            repairCorruptSatelliteState("refresh_fix");
        }
    }

    void logSatelliteFlowIfChanged()
    {
        if (!kGpsFlowDebugLog)
        {
            return;
        }

        const uint32_t now_ms = millis();
        const uint8_t parser_sats =
            static_cast<uint8_t>(std::min<uint32_t>(parser.satellites.isValid() ? parser.satellites.value() : 0U, 255U));
        const uint8_t status_in_view = status.sats_in_view;
        const uint8_t status_in_use = status.sats_in_use;
        const std::size_t snapshot_count = sat_count;
        const bool snapshot_available = data.valid || data.satellites > 0 || sat_count > 0;

        const bool changed = (parser_sats != last_logged_parser_sats) || (status_in_view != last_logged_status_in_view) ||
                             (status_in_use != last_logged_status_in_use) ||
                             (snapshot_count != last_logged_snapshot_count) ||
                             (snapshot_available != last_logged_snapshot_available);
        if (!changed && (now_ms - last_sat_diag_log_ms) < 3000U)
        {
            return;
        }

        last_sat_diag_log_ms = now_ms;
        last_logged_parser_sats = parser_sats;
        last_logged_status_in_view = status_in_view;
        last_logged_status_in_use = status_in_use;
        last_logged_snapshot_count = snapshot_count;
        last_logged_snapshot_available = snapshot_available;

        Serial.printf(
            "[T-Echo Lite][gps][flow] parser_sats=%u snapshot_count=%u status_view=%u status_use=%u data_valid=%u nmea=%u fix=%u\n",
            static_cast<unsigned>(parser_sats),
            static_cast<unsigned>(snapshot_count),
            static_cast<unsigned>(status_in_view),
            static_cast<unsigned>(status_in_use),
            static_cast<unsigned>(data.valid ? 1 : 0),
            static_cast<unsigned>(nmea_seen ? 1 : 0),
            static_cast<unsigned>(status.fix != ::gps::GnssFix::NOFIX ? 1 : 0));
    }
};

GpsRuntime::GpsRuntime()
{
    static Impl impl_storage;
    impl_ = &impl_storage;
    if (impl_)
    {
        impl_->initializeDebugGuards();
        impl_->logMemoryLayout("ctor");
    }
}

GpsRuntime::~GpsRuntime()
{
    impl_ = nullptr;
}

GpsRuntime::Impl* GpsRuntime::impl()
{
    return impl_;
}

const GpsRuntime::Impl* GpsRuntime::impl() const
{
    return impl_;
}

bool GpsRuntime::debugCheckMemoryGuard(const char* reason)
{
    if (!impl_)
    {
        return true;
    }

    auto& s = *impl_;
    if (!s.satelliteStateLooksCorrupt())
    {
        return true;
    }

    s.repairCorruptSatelliteState(reason ? reason : "debug_probe");
    return false;
}

bool GpsRuntime::start(const app::AppConfig& config)
{
    if (!begin(config))
    {
        return false;
    }
    applyConfig(config);
    return true;
}

bool GpsRuntime::begin(const app::AppConfig& config)
{
    auto& s = *impl();
    s.collection_interval_ms = config.gps_interval_ms;
    s.power_strategy = config.gps_strategy;
    s.gnss_mode = config.gps_mode;
    s.sat_mask = config.gps_sat_mask;
    s.external_nmea_output_hz = config.external_nmea_output_hz;
    s.external_nmea_sentence_mask = config.external_nmea_sentence_mask;
    s.motion_idle_timeout_ms = config.motion_config.idle_timeout_ms;
    s.motion_poll_interval_ms = config.motion_config.poll_interval_ms;
    s.motion_sensor_id = config.motion_config.sensor_id;
    s.user_enabled = config.gps_enabled;
    s.enabled = s.user_enabled;
    if (!s.initialized)
    {
        s.initialized = true;
        if (s.enabled)
        {
            const uint32_t now_ms = millis();
            (void)s.probeMotionSensor(now_ms, true);
            s.pollMotionSensor(now_ms);
            s.updateGpsPowerState(now_ms, "begin");
        }
        s.logMemoryLayout("begin");
    }
    return true;
}

void GpsRuntime::applyConfig(const app::AppConfig& config)
{
    auto& s = *impl();
    s.collection_interval_ms = config.gps_interval_ms;
    s.power_strategy = config.gps_strategy;
    s.gnss_mode = config.gps_mode;
    s.sat_mask = config.gps_sat_mask;
    s.external_nmea_output_hz = config.external_nmea_output_hz;
    s.external_nmea_sentence_mask = config.external_nmea_sentence_mask;
    s.motion_idle_timeout_ms = config.motion_config.idle_timeout_ms;
    s.motion_poll_interval_ms = config.motion_config.poll_interval_ms;
    s.motion_sensor_id = config.motion_config.sensor_id;
    s.user_enabled = config.gps_enabled;
    s.enabled = s.user_enabled;
    if (!s.enabled)
    {
        s.setGpsPower(false, "config-disabled");
        s.clearObservations();
    }
    else if (s.initialized)
    {
        const uint32_t now_ms = millis();
        (void)s.probeMotionSensor(now_ms, false);
        s.pollMotionSensor(now_ms);
        s.updateGpsPowerState(now_ms, "config");
    }
    Serial.printf(
        "[T-Echo Lite][gps] config enabled=%u powered=%u interval_ms=%lu strategy=%u mode=%u sat_mask=0x%02X external_nmea_hz=%u external_nmea_mask=0x%02X motion_idle_ms=%lu motion_poll_ms=%lu motion_sensor=%u motion_available=%u\n",
        static_cast<unsigned>(s.enabled ? 1 : 0),
        static_cast<unsigned>(s.powered ? 1 : 0),
        static_cast<unsigned long>(s.collection_interval_ms),
        static_cast<unsigned>(s.power_strategy),
        static_cast<unsigned>(s.gnss_mode),
        static_cast<unsigned>(s.sat_mask),
        static_cast<unsigned>(s.external_nmea_output_hz),
        static_cast<unsigned>(s.external_nmea_sentence_mask),
        static_cast<unsigned long>(s.motion_idle_timeout_ms),
        static_cast<unsigned long>(s.effectiveMotionPollIntervalMs()),
        static_cast<unsigned>(s.motion_sensor_id),
        static_cast<unsigned>(s.motion_available ? 1 : 0));
}

void GpsRuntime::tick()
{
    auto& s = *impl();
    if (!s.initialized)
    {
        return;
    }

    const uint32_t now_ms = millis();
    if (s.enabled)
    {
        s.pollMotionSensor(now_ms);
        s.updateGpsPowerState(now_ms, "tick");
    }

    if (!s.enabled || !s.powered)
    {
        s.logStatusIfDue();
        return;
    }

    if (s.satelliteStateLooksCorrupt())
    {
        s.repairCorruptSatelliteState("tick_pre");
    }

    while (Serial1.available() > 0)
    {
        s.nmea_seen = true;
        s.last_nmea_ms = millis();
        const char ch = static_cast<char>(Serial1.read());
        s.parser.encode(ch);
        s.processNmeaChar(ch);
    }

    s.applyTimeIfValid();
    s.refreshFix();

    if (s.satelliteStateLooksCorrupt())
    {
        s.repairCorruptSatelliteState("tick_post_refresh");
    }

    s.logSatelliteFlowIfChanged();
    s.logStatusIfDue();
}

bool GpsRuntime::isReady() const
{
    const auto& s = *impl();
    return s.initialized && s.powered;
}

::gps::GpsState GpsRuntime::data() const
{
    return impl()->data;
}

bool GpsRuntime::enabled() const
{
    return impl()->enabled;
}

bool GpsRuntime::powered() const
{
    return impl()->powered;
}

uint32_t GpsRuntime::lastMotionMs() const
{
    return impl()->last_motion_ms;
}

bool GpsRuntime::gnssSnapshot(::gps::GnssSatInfo* out,
                              std::size_t max,
                              std::size_t* out_count,
                              ::gps::GnssStatus* status) const
{
    auto& s = *const_cast<Impl*>(impl());

    if (s.satelliteStateLooksCorrupt())
    {
        s.logMemoryLayout("snapshot_pre_corrupt");
        s.repairCorruptSatelliteState("snapshot");
    }

    const bool snapshot_available = s.data.valid || s.data.satellites > 0 || s.sat_count > 0;
    if (out_count)
    {
        *out_count = 0;
    }
    if (status)
    {
        *status = s.status;
    }
    if (out && max > 0 && s.sat_count > 0)
    {
        const std::size_t copy_count = std::min<std::size_t>(max, s.sat_count);
        for (std::size_t index = 0; index < copy_count; ++index)
        {
            out[index] = s.sats[index];
        }
        if (out_count)
        {
            *out_count = copy_count;
        }
        return true;
    }
    if (snapshot_available)
    {
        Serial.printf(
            "[T-Echo Lite][gps][snapshot] available_without_copy max=%u raw_count=%u status_view=%u status_use=%u parser_sats=%u data_valid=%u\n",
            static_cast<unsigned>(max),
            static_cast<unsigned>(s.sat_count),
            static_cast<unsigned>(s.status.sats_in_view),
            static_cast<unsigned>(s.status.sats_in_use),
            static_cast<unsigned>(s.data.satellites),
            static_cast<unsigned>(s.data.valid ? 1 : 0));
    }
    return snapshot_available;
}

void GpsRuntime::setCollectionInterval(uint32_t interval_ms) { impl()->collection_interval_ms = interval_ms; }

void GpsRuntime::setPowerStrategy(uint8_t strategy)
{
    auto& s = *impl();
    s.power_strategy = strategy;
    if (s.initialized)
    {
        const uint32_t now_ms = millis();
        s.updateGpsPowerState(now_ms, "strategy");
    }
}

void GpsRuntime::setEnabled(bool enabled)
{
    auto& s = *impl();
    s.user_enabled = enabled;
    s.enabled = enabled;
    if (!s.enabled)
    {
        s.setGpsPower(false, "user-disabled");
        s.clearObservations();
        return;
    }
    if (s.initialized)
    {
        const uint32_t now_ms = millis();
        (void)s.probeMotionSensor(now_ms, true);
        s.pollMotionSensor(now_ms);
        s.updateGpsPowerState(now_ms, "user-enabled");
    }
}

void GpsRuntime::setConfig(uint8_t mode, uint8_t sat_mask)
{
    auto& s = *impl();
    s.gnss_mode = mode;
    s.sat_mask = sat_mask;
}

void GpsRuntime::setExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    impl()->external_nmea_output_hz = output_hz;
    impl()->external_nmea_sentence_mask = sentence_mask;
}

void GpsRuntime::setMotionIdleTimeout(uint32_t timeout_ms)
{
    auto& s = *impl();
    s.motion_idle_timeout_ms = timeout_ms;
    if (s.initialized)
    {
        const uint32_t now_ms = millis();
        s.updateGpsPowerState(now_ms, "motion-idle");
    }
}

void GpsRuntime::setMotionSensorId(uint8_t sensor_id)
{
    auto& s = *impl();
    s.motion_sensor_id = sensor_id;
    if (s.initialized)
    {
        const uint32_t now_ms = millis();
        (void)s.probeMotionSensor(now_ms, true);
        s.updateGpsPowerState(now_ms, "motion-sensor");
    }
}

void GpsRuntime::suspend()
{
    auto& s = *impl();
    s.setGpsPower(false, "suspend");
    s.enabled = false;
    s.clearObservations();
}

void GpsRuntime::resume()
{
    auto& s = *impl();
    s.enabled = s.user_enabled;
    if (!s.enabled)
    {
        s.clearObservations();
    }
    else if (s.initialized)
    {
        const uint32_t now_ms = millis();
        s.pollMotionSensor(now_ms);
        s.updateGpsPowerState(now_ms, "resume");
    }
}

void GpsRuntime::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (epoch_s < kMinValidEpochSeconds)
    {
        return;
    }

    auto& s = *impl();
    const uint32_t prev_epoch_s = s.epoch_base_s;
    s.epoch_base_s = epoch_s;
    s.epoch_base_ms = millis();
    s.time_synced = true;
    s.last_time_sync_epoch_logged = epoch_s;
    s.last_time_sync_log_ms = s.epoch_base_ms;
    s.last_time_sync_source = TimeSyncSource::External;
    Serial.printf("[T-Echo Lite][gps] time sync source=external epoch=%lu prev=%lu\n",
                  static_cast<unsigned long>(epoch_s),
                  static_cast<unsigned long>(prev_epoch_s));
    syncSystemClockFromEpoch(epoch_s);
}

uint32_t GpsRuntime::currentEpochSeconds() const
{
    const uint32_t system_epoch_s = readSystemEpochSeconds();
    if (system_epoch_s >= kMinValidEpochSeconds)
    {
        return system_epoch_s;
    }

    const auto& s = *impl();
    if (!s.time_synced || s.epoch_base_s == 0)
    {
        return 0;
    }
    const uint32_t elapsed_s = (millis() - s.epoch_base_ms) / 1000U;
    return s.epoch_base_s + elapsed_s;
}

bool GpsRuntime::isRtcReady() const
{
    const auto& s = *impl();
    return s.time_synced && currentEpochSeconds() >= kMinValidEpochSeconds;
}

} // namespace boards::t_echo_lite
