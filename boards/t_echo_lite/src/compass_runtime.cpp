#include "boards/t_echo_lite/compass_runtime.h"

#include "boards/t_echo_lite/board_profile.h"
#include "boards/t_echo_lite/t_echo_lite_board.h"

#include <Arduino.h>
#include <Wire.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace boards::t_echo_lite
{
namespace
{

constexpr uint32_t kProbeRetryMs = 2000UL;
constexpr uint32_t kSampleMinIntervalMs = 70UL;
constexpr uint8_t kIcm20948WhoAmI = 0xEA;
constexpr uint8_t kIcmBankSelectReg = 0x7F;
constexpr uint8_t kIcmWhoAmIReg = 0x00;
constexpr uint8_t kIcmUserCtrlReg = 0x03;
constexpr uint8_t kIcmPwrMgmt1Reg = 0x06;
constexpr uint8_t kIcmPwrMgmt2Reg = 0x07;
constexpr uint8_t kIcmAccelXoutHReg = 0x2D;
constexpr uint8_t kIcmExtSlvSensData00Reg = 0x3B;
constexpr uint8_t kIcmOdrAlignEnReg = 0x09;
constexpr uint8_t kIcmI2cMstCtrlReg = 0x01;
constexpr uint8_t kIcmI2cSlv0AddrReg = 0x03;
constexpr uint8_t kIcmI2cSlv0RegReg = 0x04;
constexpr uint8_t kIcmI2cSlv0CtrlReg = 0x05;
constexpr uint8_t kIcmI2cSlv0DoReg = 0x06;
constexpr uint8_t kIcmI2cMstEn = 0x20;
constexpr uint8_t kIcmI2cMstRst = 0x02;
constexpr uint8_t kAk09916Address = 0x0C;
constexpr uint8_t kAk09916Read = 0x80;
constexpr uint8_t kAk09916Wia1Reg = 0x00;
constexpr uint8_t kAk09916HxlReg = 0x11;
constexpr uint8_t kAk09916St2Overflow = 0x08;
constexpr uint8_t kAk09916Cntl2Reg = 0x31;
constexpr uint8_t kAk09916Cntl3Reg = 0x32;
constexpr uint16_t kAk09916WhoAmI1 = 0x4809;
constexpr uint16_t kAk09916WhoAmI2 = 0x0948;
constexpr uint8_t kAk09916ModeContinuous50Hz = 0x06;
constexpr float kPi = 3.14159265358979323846F;

float normalizeDeg(float deg)
{
    while (deg < 0.0F)
    {
        deg += 360.0F;
    }
    while (deg >= 360.0F)
    {
        deg -= 360.0F;
    }
    return deg;
}

float angleDeltaDeg(float from_deg, float to_deg)
{
    float delta = normalizeDeg(to_deg) - normalizeDeg(from_deg);
    if (delta > 180.0F)
    {
        delta -= 360.0F;
    }
    else if (delta < -180.0F)
    {
        delta += 360.0F;
    }
    return delta;
}

bool i2cAddressResponds(TwoWire& wire, uint8_t address)
{
    wire.beginTransmission(address);
    return wire.endTransmission() == 0;
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
    return i2cWriteByte(wire, address, kIcmBankSelectReg, static_cast<uint8_t>((bank & 0x03U) << 4));
}

bool icmReadByte(TwoWire& wire, uint8_t address, uint8_t bank, uint8_t reg, uint8_t* out)
{
    return icmSelectBank(wire, address, bank) && i2cReadBytes(wire, address, reg, out, 1);
}

bool icmReadBytes(TwoWire& wire, uint8_t address, uint8_t bank, uint8_t reg, uint8_t* out, std::size_t len)
{
    return icmSelectBank(wire, address, bank) && i2cReadBytes(wire, address, reg, out, len);
}

bool icmWriteByte(TwoWire& wire, uint8_t address, uint8_t bank, uint8_t reg, uint8_t value)
{
    return icmSelectBank(wire, address, bank) && i2cWriteByte(wire, address, reg, value);
}

bool icmEnableI2cMaster(TwoWire& wire, uint8_t address)
{
    uint8_t user_ctrl = 0;
    if (!icmReadByte(wire, address, 0, kIcmUserCtrlReg, &user_ctrl))
    {
        return false;
    }
    user_ctrl |= kIcmI2cMstEn;
    return icmWriteByte(wire, address, 0, kIcmUserCtrlReg, user_ctrl) &&
           icmWriteByte(wire, address, 3, kIcmI2cMstCtrlReg, 0x07);
}

void icmResetI2cMaster(TwoWire& wire, uint8_t address)
{
    uint8_t user_ctrl = 0;
    if (icmReadByte(wire, address, 0, kIcmUserCtrlReg, &user_ctrl))
    {
        (void)icmWriteByte(wire, address, 0, kIcmUserCtrlReg, static_cast<uint8_t>(user_ctrl | kIcmI2cMstRst));
        delay(10);
    }
}

bool icmEnableAk09916DataRead(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t bytes)
{
    if (bytes == 0 || bytes > 16)
    {
        return false;
    }
    return icmWriteByte(wire, address, 3, kIcmI2cSlv0AddrReg, static_cast<uint8_t>(kAk09916Address | kAk09916Read)) &&
           icmWriteByte(wire, address, 3, kIcmI2cSlv0RegReg, reg) &&
           icmWriteByte(wire, address, 3, kIcmI2cSlv0CtrlReg, static_cast<uint8_t>(0x80U | bytes));
}

bool icmWriteAk09916Register(TwoWire& wire, uint8_t address, uint8_t reg, uint8_t value)
{
    const bool ok =
        icmWriteByte(wire, address, 3, kIcmI2cSlv0AddrReg, kAk09916Address) &&
        icmWriteByte(wire, address, 3, kIcmI2cSlv0RegReg, reg) &&
        icmWriteByte(wire, address, 3, kIcmI2cSlv0DoReg, value) &&
        icmWriteByte(wire, address, 3, kIcmI2cSlv0CtrlReg, 0x81);
    delay(10);
    return ok;
}

bool icmReadAk09916WhoAmI(TwoWire& wire, uint8_t address, uint16_t* out)
{
    if (!out || !icmEnableAk09916DataRead(wire, address, kAk09916Wia1Reg, 2))
    {
        return false;
    }
    delay(10);

    uint8_t raw[2] = {};
    if (!icmReadBytes(wire, address, 0, kIcmExtSlvSensData00Reg, raw, sizeof(raw)))
    {
        return false;
    }

    *out = static_cast<uint16_t>((static_cast<uint16_t>(raw[0]) << 8) | raw[1]);
    return true;
}

bool icmConfigureAk09916(TwoWire& wire, uint8_t address, uint16_t* out_who)
{
    if (!icmEnableI2cMaster(wire, address))
    {
        return false;
    }
    (void)icmWriteByte(wire, address, 2, kIcmOdrAlignEnReg, 0x01);

    bool mag_found = false;
    uint16_t who = 0;
    for (uint8_t tries = 0; tries < 10; ++tries)
    {
        delay(10);
        if (icmReadAk09916WhoAmI(wire, address, &who) &&
            (who == kAk09916WhoAmI1 || who == kAk09916WhoAmI2))
        {
            mag_found = true;
            break;
        }
        icmResetI2cMaster(wire, address);
        (void)icmEnableI2cMaster(wire, address);
    }

    if (out_who)
    {
        *out_who = who;
    }
    if (!mag_found)
    {
        return false;
    }

    if (!icmWriteAk09916Register(wire, address, kAk09916Cntl3Reg, 0x01))
    {
        return false;
    }
    delay(100);
    if (!icmWriteAk09916Register(wire, address, kAk09916Cntl2Reg, kAk09916ModeContinuous50Hz))
    {
        return false;
    }
    return icmEnableAk09916DataRead(wire, address, kAk09916HxlReg, 8);
}

bool readIcmAccel(TwoWire& wire, uint8_t address, CompassRawSample* out)
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

    out->ax = static_cast<int16_t>((static_cast<uint16_t>(raw[0]) << 8) | raw[1]);
    out->ay = static_cast<int16_t>((static_cast<uint16_t>(raw[2]) << 8) | raw[3]);
    out->az = static_cast<int16_t>((static_cast<uint16_t>(raw[4]) << 8) | raw[5]);
    return true;
}

bool readAk09916Mag(TwoWire& wire, uint8_t address, CompassRawSample* out)
{
    if (!out)
    {
        return false;
    }

    uint8_t raw[8] = {};
    if (!icmReadBytes(wire, address, 0, kIcmExtSlvSensData00Reg, raw, sizeof(raw)))
    {
        return false;
    }
    if ((raw[7] & kAk09916St2Overflow) != 0)
    {
        return false;
    }

    out->mx = static_cast<int16_t>((static_cast<uint16_t>(raw[1]) << 8) | raw[0]);
    out->my = static_cast<int16_t>((static_cast<uint16_t>(raw[3]) << 8) | raw[2]);
    out->mz = static_cast<int16_t>((static_cast<uint16_t>(raw[5]) << 8) | raw[4]);
    return true;
}

uint8_t calibrationPercentForSpan(int32_t span_x, int32_t span_y, int32_t span_z)
{
    const int32_t avg_span = (span_x + span_y + span_z) / 3;
    int32_t percent = (avg_span * 100) / 900;
    const int32_t min_span = std::min(span_x, std::min(span_y, span_z));
    if (min_span < 120)
    {
        percent = std::min<int32_t>(percent, 35);
    }
    else if (min_span < 260)
    {
        percent = std::min<int32_t>(percent, 70);
    }
    return static_cast<uint8_t>(std::max<int32_t>(0, std::min<int32_t>(100, percent)));
}

} // namespace

platform::ui::compass::CompassState CompassRuntime::state()
{
    const uint32_t now_ms = millis();
    platform::ui::compass::CompassState out{};
    out.supported = true;

    if (!available_ && !probe(now_ms, false))
    {
        out.available = false;
        return out;
    }

    out.available = available_;
    if (available_ && (last_sample_ms_ == 0 || (now_ms - last_sample_ms_) >= kSampleMinIntervalMs))
    {
        CompassRawSample sample{};
        if (readSample(&sample))
        {
            read_fail_count_ = 0;
            updateCalibration(sample);
            updateHeading(sample, now_ms);
        }
        else if (read_fail_count_ < 255)
        {
            ++read_fail_count_;
        }
        if (read_fail_count_ >= 8)
        {
            Serial.printf("[T-Echo Lite][compass] read failed repeatedly; disabling compass until reprobe\n");
            available_ = false;
            probe_done_ = false;
            imu_address_ = 0;
            heading_valid_ = false;
            read_fail_count_ = 0;
        }
    }

    out.available = available_;
    out.heading_valid = heading_valid_;
    out.heading_deg = heading_deg_;
    out.magnetic_ok = magnetic_ok_;
    out.flat = flat_;
    out.calibration_percent = calibration_percent_;
    out.calibrated = calibration_percent_ >= 60;
    out.age_ms = last_sample_ms_ == 0 ? 0xFFFFFFFFUL : now_ms - last_sample_ms_;
    return out;
}

bool CompassRuntime::probe(uint32_t now_ms, bool force_log)
{
    if (available_)
    {
        return true;
    }
    if (probe_done_ && !force_log && (now_ms - last_probe_ms_) < kProbeRetryMs)
    {
        return false;
    }
    probe_done_ = true;
    last_probe_ms_ = now_ms;

    auto& board = TEchoLiteBoard::instance();
    if (!board.ensureI2cReady())
    {
        return false;
    }
    TEchoLiteBoard::I2cGuard guard(board, 80);
    if (!guard)
    {
        return false;
    }

    auto& wire = board.i2cWire();
    const auto& motion = kBoardProfile.motion;
    const uint8_t addresses[2] = {motion.primary_address, motion.secondary_address};
    for (uint8_t address : addresses)
    {
        if (address == 0 || !i2cAddressResponds(wire, address))
        {
            continue;
        }

        uint8_t who = 0;
        if (!icmReadByte(wire, address, 0, kIcmWhoAmIReg, &who) || who != kIcm20948WhoAmI)
        {
            continue;
        }

        (void)icmWriteByte(wire, address, 0, kIcmPwrMgmt1Reg, 0x01);
        (void)icmWriteByte(wire, address, 0, kIcmPwrMgmt2Reg, 0x00);
        delay(10);

        uint16_t mag_who = 0;
        if (!icmConfigureAk09916(wire, address, &mag_who))
        {
            if (force_log)
            {
                Serial.printf("[T-Echo Lite][compass] ICM20948 found but AK09916 unavailable who=0x%04X\n",
                              static_cast<unsigned>(mag_who));
            }
            continue;
        }

        imu_address_ = address;
        available_ = true;
        heading_valid_ = false;
        read_fail_count_ = 0;
        last_sample_ms_ = 0;
        Serial.printf("[T-Echo Lite][compass] ICM20948 compass ready addr=0x%02X mag_who=0x%04X\n",
                      static_cast<unsigned>(imu_address_),
                      static_cast<unsigned>(mag_who));
        return true;
    }

    imu_address_ = 0;
    available_ = false;
    heading_valid_ = false;
    if (force_log)
    {
        Serial.printf("[T-Echo Lite][compass] optional ICM20948 compass not detected\n");
    }
    return false;
}

bool CompassRuntime::readSample(CompassRawSample* out)
{
    if (!out || !available_ || imu_address_ == 0)
    {
        return false;
    }

    auto& board = TEchoLiteBoard::instance();
    if (!board.ensureI2cReady())
    {
        return false;
    }
    TEchoLiteBoard::I2cGuard guard(board, 40);
    if (!guard)
    {
        return false;
    }

    auto& wire = board.i2cWire();
    if (!readIcmAccel(wire, imu_address_, out))
    {
        return false;
    }
    return readAk09916Mag(wire, imu_address_, out);
}

void CompassRuntime::updateCalibration(const CompassRawSample& sample)
{
    const int16_t values[3] = {sample.mx, sample.my, sample.mz};
    if (!calibration_initialized_)
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            mag_min_[i] = values[i];
            mag_max_[i] = values[i];
        }
        calibration_initialized_ = true;
        calibration_percent_ = 0;
        return;
    }

    for (std::size_t i = 0; i < 3; ++i)
    {
        mag_min_[i] = std::min(mag_min_[i], values[i]);
        mag_max_[i] = std::max(mag_max_[i], values[i]);
    }

    calibration_percent_ = calibrationPercentForSpan(
        static_cast<int32_t>(mag_max_[0]) - mag_min_[0],
        static_cast<int32_t>(mag_max_[1]) - mag_min_[1],
        static_cast<int32_t>(mag_max_[2]) - mag_min_[2]);
}

void CompassRuntime::updateHeading(const CompassRawSample& sample, uint32_t now_ms)
{
    const float mag_offsets[3] = {
        (static_cast<float>(mag_min_[0]) + static_cast<float>(mag_max_[0])) * 0.5F,
        (static_cast<float>(mag_min_[1]) + static_cast<float>(mag_max_[1])) * 0.5F,
        (static_cast<float>(mag_min_[2]) + static_cast<float>(mag_max_[2])) * 0.5F,
    };
    const bool use_offsets = calibration_percent_ >= 20;
    const float mx = static_cast<float>(sample.mx) - (use_offsets ? mag_offsets[0] : 0.0F);
    const float my = static_cast<float>(sample.my) - (use_offsets ? mag_offsets[1] : 0.0F);
    const float mz = static_cast<float>(sample.mz) - (use_offsets ? mag_offsets[2] : 0.0F);
    const float mag_norm = std::sqrt((mx * mx) + (my * my) + (mz * mz));

    magnetic_ok_ = mag_norm >= 40.0F && mag_norm <= 5000.0F;
    const float accel_norm = std::sqrt((static_cast<float>(sample.ax) * sample.ax) +
                                       (static_cast<float>(sample.ay) * sample.ay) +
                                       (static_cast<float>(sample.az) * sample.az));
    flat_ = accel_norm > 1.0F && (std::fabs(static_cast<float>(sample.az)) / accel_norm) >= 0.70F;

    if (!magnetic_ok_)
    {
        heading_valid_ = false;
        last_sample_ms_ = now_ms;
        return;
    }

    const float raw_heading = normalizeDeg((std::atan2(my, mx) * 180.0F / kPi) + 90.0F);
    if (!heading_valid_)
    {
        heading_deg_ = raw_heading;
        heading_valid_ = true;
    }
    else
    {
        const float delta = angleDeltaDeg(heading_deg_, raw_heading);
        if (std::fabs(delta) >= 1.5F)
        {
            heading_deg_ = normalizeDeg(heading_deg_ + (delta * 0.45F));
        }
    }
    last_sample_ms_ = now_ms;
}

} // namespace boards::t_echo_lite
