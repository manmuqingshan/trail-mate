#pragma once

#include <cstdint>

namespace boards::wio_tracker_l2
{

inline constexpr char kBoardName[] = "Seeed Wio Tracker L2";
inline constexpr uint32_t kFlashBytes = 16U * 1024U * 1024U;
inline constexpr uint32_t kPsramBytes = 8U * 1024U * 1024U;
inline constexpr uint16_t kPanelWidth = 240;
inline constexpr uint16_t kPanelHeight = 320;

namespace gpio
{
inline constexpr int kI2cSda = 47;
inline constexpr int kI2cScl = 48;
inline constexpr int kExpanderInterrupt = 45;
inline constexpr int kBootButton = 0;
inline constexpr int kRadioSck = 4;
inline constexpr int kRadioMiso = 5;
inline constexpr int kRadioMosi = 6;
inline constexpr int kRadioCs = 21;
inline constexpr int kRadioReset = 7;
inline constexpr int kRadioBusy = 8;
inline constexpr int kRadioDio1 = 9;
inline constexpr int kDisplayClock = 42;
inline constexpr int kDisplayIo0 = 41;
inline constexpr int kDisplayIo1 = 40;
inline constexpr int kDisplayIo2 = 39;
inline constexpr int kDisplayIo3 = 38;
inline constexpr int kDisplayCs = 46;
inline constexpr int kGpsRx = 18;
inline constexpr int kGpsTx = 17;
inline constexpr int kSdClock = 2;
inline constexpr int kSdCommand = 3;
inline constexpr int kSdData0 = 1;
inline constexpr int kAudioMclk = 10;
inline constexpr int kAudioBclk = 11;
inline constexpr int kAudioLrclk = 12;
inline constexpr int kAudioDataOut = 16;
} // namespace gpio

namespace i2c
{
inline constexpr uint32_t kFrequencyHz = 100000;
inline constexpr uint8_t kExpanderAddress = 0x21;
inline constexpr uint8_t kBacklightAddress = 0x2C;
inline constexpr uint8_t kBatteryAdcAddress = 0x48;
inline constexpr uint8_t kTouchAddress = 0x5D;
inline constexpr uint8_t kTouchAlternateAddress = 0x14;
inline constexpr uint8_t kUsbControllerAddress = 0x22;
} // namespace i2c

// These are linear expander indexes, not ESP GPIOs. See BOARD.md for the
// conversion from Seeed's port/bit notation, e.g. P15 -> (1 * 8 + 5) -> 13.
enum class ExpanderPin : uint8_t
{
    WakeButton = 0,
    I2cInterrupt = 1,
    SdDetect = 2,
    TouchInterrupt = 3,
    DisplayControl = 4,
    DisplayPower = 5,
    DisplayReset = 6,
    GrovePower = 7,
    TouchReset = 8,
    GpsReset = 9,
    UserLed = 10,
    UsbOtgEnable = 11,
    AudioAmplifierEnable = 12,
    GpsPower = 13,
    SdPower = 14,
    BatteryMeasurementEnable = 15,
};

} // namespace boards::wio_tracker_l2
