#include "boards/t_echo_lite/t_echo_lite_board.h"

#include "boards/t_echo_lite/assets/pager_notification_adpcm.h"
#include "boards/t_echo_lite/board_profile.h"
#include "boards/t_echo_lite/gps_runtime.h"
#include "boards/t_echo_lite/input_runtime.h"
#include "boards/t_echo_lite/settings_store.h"
#include "boards/t_echo_lite/sx1262_radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "ui/mono/runtime.h"

#include <Adafruit_EPD.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_i2s.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace boards::t_echo_lite
{
namespace
{

void writeLed(int pin, bool active_high, bool on)
{
    if (pin < 0)
    {
        return;
    }
    pinMode(pin, OUTPUT);
    digitalWrite(pin, (on == active_high) ? HIGH : LOW);
}

struct LedColor
{
    const char* label;
    bool red;
    bool green;
    bool blue;
};

template <typename T, size_t N>
constexpr size_t arrayCount(const T (&)[N])
{
    return N;
}

constexpr LedColor kLedColors[] = {
    {"OFF", false, false, false},
    {"RED", true, false, false},
    {"GREEN", false, true, false},
    {"BLUE", false, false, true},
    {"WHITE", true, true, true},
    {"AMBER", true, true, false},
    {"CYAN", false, true, true},
    {"MAGENTA", true, false, true},
};

struct RegWrite
{
    uint8_t reg;
    uint8_t value;
};

constexpr uint8_t kEs8311ResetReg00 = 0x00;
constexpr uint8_t kEs8311ClkReg01 = 0x01;
constexpr uint8_t kEs8311ClkReg02 = 0x02;
constexpr uint8_t kEs8311ClkReg03 = 0x03;
constexpr uint8_t kEs8311ClkReg04 = 0x04;
constexpr uint8_t kEs8311ClkReg05 = 0x05;
constexpr uint8_t kEs8311ClkReg06 = 0x06;
constexpr uint8_t kEs8311ClkReg07 = 0x07;
constexpr uint8_t kEs8311ClkReg08 = 0x08;
constexpr uint8_t kEs8311SdpInReg09 = 0x09;
constexpr uint8_t kEs8311SdpOutReg0A = 0x0A;
constexpr uint8_t kEs8311SystemReg0B = 0x0B;
constexpr uint8_t kEs8311SystemReg0C = 0x0C;
constexpr uint8_t kEs8311SystemReg0D = 0x0D;
constexpr uint8_t kEs8311SystemReg0E = 0x0E;
constexpr uint8_t kEs8311SystemReg10 = 0x10;
constexpr uint8_t kEs8311SystemReg11 = 0x11;
constexpr uint8_t kEs8311SystemReg12 = 0x12;
constexpr uint8_t kEs8311SystemReg13 = 0x13;
constexpr uint8_t kEs8311SystemReg14 = 0x14;
constexpr uint8_t kEs8311AdcReg15 = 0x15;
constexpr uint8_t kEs8311AdcReg16 = 0x16;
constexpr uint8_t kEs8311AdcReg17 = 0x17;
constexpr uint8_t kEs8311AdcReg1B = 0x1B;
constexpr uint8_t kEs8311AdcReg1C = 0x1C;
constexpr uint8_t kEs8311DacReg31 = 0x31;
constexpr uint8_t kEs8311DacReg32 = 0x32;
constexpr uint8_t kEs8311DacReg37 = 0x37;
constexpr uint8_t kEs8311GpioReg44 = 0x44;
constexpr uint8_t kEs8311GpReg45 = 0x45;

constexpr uint8_t kAw21009Gcr = 0x20;
constexpr uint8_t kAw21009BrightnessBase = 0x21;
constexpr uint8_t kAw21009Update = 0x45;
constexpr uint8_t kAw21009ScalingBase = 0x46;
constexpr uint8_t kAw21009GlobalCurrent = 0x58;
constexpr uint8_t kAw21009Reset = 0x70;
constexpr uint8_t kAw21009ChannelCount = 9;

constexpr uint32_t kMessageToneSampleRateHz = 44100;
constexpr uint16_t kMessageToneMaxI2sWords = 14336;
static_assert(kMessageToneSampleRateHz % assets::kPagerNotificationSampleRateHz == 0,
              "Pager notification asset sample rate must divide the I2S sample rate");
constexpr uint8_t kMessageToneSampleRepeat =
    static_cast<uint8_t>(kMessageToneSampleRateHz / assets::kPagerNotificationSampleRateHz);
constexpr uint32_t kEpaperPresentMinIntervalMs = 50UL;
constexpr int kEpaperMirrorPixels = ::boards::t_echo_lite::kBoardProfile.epaper.width *
                                    ::boards::t_echo_lite::kBoardProfile.epaper.height;
constexpr size_t kEpaperMirrorBytes = static_cast<size_t>((kEpaperMirrorPixels + 7) / 8);
uint32_t s_message_tone_i2s_buffer[kMessageToneMaxI2sWords] = {};

void writeLedColor(uint8_t color_index, bool on, const char* reason = nullptr, uint32_t pulse_ms = 0)
{
    const auto& leds = kBoardProfile.leds;
    const LedColor& color = kLedColors[color_index % arrayCount(kLedColors)];
    if (reason)
    {
        Serial.printf("[t-echo-lite][led] reason=%s on=%u color=%u label=%s pulse_ms=%lu uptime_ms=%lu\n",
                      reason,
                      on ? 1U : 0U,
                      static_cast<unsigned>(color_index),
                      color.label,
                      static_cast<unsigned long>(pulse_ms),
                      static_cast<unsigned long>(millis()));
    }
    writeLed(leds.red, leds.active_high, on && color.red);
    writeLed(leds.green, leds.active_high, on && color.green);
    writeLed(leds.blue, leds.active_high, on && color.blue);
}

uint8_t es8311VolumeRegister(uint8_t volume_percent)
{
    const uint8_t volume = volume_percent > 100U ? 100U : volume_percent;
    if (volume == 0U)
    {
        return 0U;
    }
    const uint8_t effective_volume = std::max<uint8_t>(volume, 100U);
    return static_cast<uint8_t>(80U + ((static_cast<uint16_t>(effective_volume) * 111U) / 100U));
}

bool writeI2cRegister(TEchoLiteBoard& board, uint8_t address, uint8_t reg, uint8_t value)
{
    TEchoLiteBoard::I2cGuard guard(board, 100);
    if (!guard)
    {
        return false;
    }
    TwoWire& wire = board.i2cWire();
    wire.beginTransmission(address);
    wire.write(reg);
    wire.write(value);
    return wire.endTransmission() == 0;
}

bool writeEs8311(TEchoLiteBoard& board, uint8_t reg, uint8_t value)
{
    return writeI2cRegister(board, kBoardProfile.audio.codec_address, reg, value);
}

bool writeAw21009(TEchoLiteBoard& board, uint8_t reg, uint8_t value)
{
    return writeI2cRegister(board, kBoardProfile.keyboard_backlight.address, reg, value);
}

bool configureEs8311ForMessageTone(TEchoLiteBoard& board, uint8_t volume_percent)
{
    const uint8_t dac_volume = es8311VolumeRegister(volume_percent);
    if (!writeEs8311(board, kEs8311ResetReg00, 0x1F))
    {
        return false;
    }
    delay(20);
    if (!writeEs8311(board, kEs8311ResetReg00, 0x00) ||
        !writeEs8311(board, kEs8311ResetReg00, 0x80))
    {
        return false;
    }

    const RegWrite sequence[] = {
        {kEs8311ClkReg01, 0x3F},
        {kEs8311ResetReg00, 0x84},
        {kEs8311ClkReg02, 0x18},
        {kEs8311ClkReg03, 0x10},
        {kEs8311ClkReg04, 0x10},
        {kEs8311ClkReg05, 0x00},
        {kEs8311ClkReg06, 0x03},
        {kEs8311ClkReg07, 0x00},
        {kEs8311ClkReg08, 0xFF},
        {kEs8311SdpInReg09, 0x0C},
        {kEs8311SdpOutReg0A, 0x0C},
        {kEs8311SystemReg0D, 0x01},
        {kEs8311SystemReg0E, 0x02},
        {kEs8311SystemReg12, 0x00},
        {kEs8311SystemReg13, 0x10},
        {kEs8311SystemReg14, 0x1A},
        {kEs8311AdcReg16, 0x03},
        {kEs8311AdcReg17, 0xBF},
        {kEs8311AdcReg1C, 0x2A},
        {kEs8311DacReg31, 0x00},
        {kEs8311DacReg32, dac_volume},
        {kEs8311DacReg37, 0x08},
    };

    for (const RegWrite& write : sequence)
    {
        if (!writeEs8311(board, write.reg, write.value))
        {
            return false;
        }
    }
    return true;
}

bool configureAw21009(TEchoLiteBoard& board)
{
    if (!writeAw21009(board, kAw21009Reset, 0x00))
    {
        return false;
    }
    delay(2);

    bool ok = true;
    ok &= writeAw21009(board, kAw21009Gcr, 0x85);           // APSE=1, PWMRES=12-bit, CHIPEN=1
    ok &= writeAw21009(board, kAw21009GlobalCurrent, 0x40); // Conservative current limit
    for (uint8_t channel = 0; channel < kAw21009ChannelCount; ++channel)
    {
        ok &= writeAw21009(board, static_cast<uint8_t>(kAw21009ScalingBase + channel), 0xFF);
    }
    ok &= writeAw21009(board, kAw21009Update, 0x00);
    return ok;
}

bool applyAw21009Brightness(TEchoLiteBoard& board, uint16_t brightness)
{
    const uint16_t value = brightness > 4095U ? 4095U : brightness;
    bool ok = true;
    for (uint8_t channel = 0; channel < kAw21009ChannelCount; ++channel)
    {
        const uint8_t base = static_cast<uint8_t>(kAw21009BrightnessBase + (channel * 2U));
        ok &= writeAw21009(board, base, static_cast<uint8_t>(value & 0xFFU));
        ok &= writeAw21009(board, static_cast<uint8_t>(base + 1U), static_cast<uint8_t>((value >> 8) & 0x0FU));
    }
    ok &= writeAw21009(board, kAw21009Update, 0x00);
    return ok;
}

constexpr int8_t kImaAdpcmIndexTable[16] = {
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
    -1,
    -1,
    -1,
    -1,
    2,
    4,
    6,
    8,
};

constexpr int16_t kImaAdpcmStepTable[89] = {
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    16,
    17,
    19,
    21,
    23,
    25,
    28,
    31,
    34,
    37,
    41,
    45,
    50,
    55,
    60,
    66,
    73,
    80,
    88,
    97,
    107,
    118,
    130,
    143,
    157,
    173,
    190,
    209,
    230,
    253,
    279,
    307,
    337,
    371,
    408,
    449,
    494,
    544,
    598,
    658,
    724,
    796,
    876,
    963,
    1060,
    1166,
    1282,
    1411,
    1552,
    1707,
    1878,
    2066,
    2272,
    2499,
    2749,
    3024,
    3327,
    3660,
    4026,
    4428,
    4871,
    5358,
    5894,
    6484,
    7132,
    7845,
    8630,
    9493,
    10442,
    11487,
    12635,
    13899,
    15289,
    16818,
    18500,
    20350,
    22385,
    24623,
    27086,
    29794,
    32767,
};

struct AdpcmPlaybackState
{
    int16_t predictor = assets::kPagerNotificationInitialSample;
    int16_t current_sample = assets::kPagerNotificationInitialSample;
    uint8_t step_index = 0;
    uint8_t repeat_remaining = 0;
    uint32_t sample_index = 0;
};

int16_t clampPcmSample(int32_t sample)
{
    if (sample > 32767)
    {
        return 32767;
    }
    if (sample < -32768)
    {
        return -32768;
    }
    return static_cast<int16_t>(sample);
}

int16_t nextPagerNotificationSample(AdpcmPlaybackState& state)
{
    if (state.sample_index == 0)
    {
        state.sample_index = 1;
        return state.current_sample;
    }

    const uint32_t nibble_index = state.sample_index - 1U;
    const uint8_t encoded = assets::kPagerNotificationAdpcmData[nibble_index / 2U];
    const uint8_t code = (nibble_index & 1U) == 0U ? (encoded & 0x0FU) : (encoded >> 4);
    const int16_t step = kImaAdpcmStepTable[state.step_index];
    int32_t diff = step >> 3;
    if ((code & 0x01U) != 0)
    {
        diff += step >> 2;
    }
    if ((code & 0x02U) != 0)
    {
        diff += step >> 1;
    }
    if ((code & 0x04U) != 0)
    {
        diff += step;
    }
    const int32_t predictor = (code & 0x08U) != 0
                                  ? static_cast<int32_t>(state.predictor) - diff
                                  : static_cast<int32_t>(state.predictor) + diff;
    state.predictor = clampPcmSample(predictor);
    state.current_sample = state.predictor;

    const int16_t next_index = static_cast<int16_t>(state.step_index) + kImaAdpcmIndexTable[code & 0x0FU];
    state.step_index = static_cast<uint8_t>(std::max<int16_t>(0, std::min<int16_t>(88, next_index)));
    ++state.sample_index;
    return state.current_sample;
}

uint16_t fillPagerNotificationBuffer(AdpcmPlaybackState& state)
{
    uint16_t word_count = 0;
    while (word_count < kMessageToneMaxI2sWords &&
           (state.sample_index < assets::kPagerNotificationSampleCount || state.repeat_remaining > 0U))
    {
        if (state.repeat_remaining == 0U)
        {
            if (state.sample_index >= assets::kPagerNotificationSampleCount)
            {
                break;
            }
            state.current_sample = nextPagerNotificationSample(state);
            state.repeat_remaining = kMessageToneSampleRepeat;
        }

        const uint16_t packed_sample = static_cast<uint16_t>(state.current_sample);
        s_message_tone_i2s_buffer[word_count] =
            static_cast<uint32_t>(packed_sample) | (static_cast<uint32_t>(packed_sample) << 16);
        ++word_count;
        --state.repeat_remaining;
    }
    return word_count;
}

void stopI2s()
{
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_STOP);
    const uint32_t started_ms = millis();
    while (!nrf_i2s_event_check(NRF_I2S, NRF_I2S_EVENT_STOPPED) &&
           (millis() - started_ms) < 50U)
    {
        delay(1);
    }
    nrf_i2s_disable(NRF_I2S);
}

} // namespace

TEchoLiteBoard& TEchoLiteBoard::instance()
{
    static TEchoLiteBoard board_instance;
    return board_instance;
}

TEchoLiteBoard::TEchoLiteBoard()
    : gps_runtime_(new GpsRuntime()),
      input_runtime_(new InputRuntime())
{
}

TEchoLiteBoard::~TEchoLiteBoard() = default;

uint32_t TEchoLiteBoard::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (initialized_)
    {
        return 1U;
    }

    initializeBoardHardware();
    ensureI2cReady();
    message_tone_volume_ = ::boards::t_echo_lite::settings_store::loadMessageToneVolume();
    status_led_color_index_ = static_cast<uint8_t>(
        ::boards::t_echo_lite::settings_store::loadStatusLedColor() % statusLedColorCount());
    keyboard_light_enabled_ = ::boards::t_echo_lite::settings_store::loadKeyboardLightEnabled();
    message_keyboard_light_enabled_ = ::boards::t_echo_lite::settings_store::loadMessageKeyboardLightEnabled();
    writeLedColor(status_led_color_index_, status_led_color_index_ != 0U);
    applyKeyboardBacklight();
    initialized_ = true;
    return 1U;
}

void TEchoLiteBoard::initializeBoardHardware()
{
    const auto& profile = kBoardProfile;
    enablePeripheralRail();

    writeLed(profile.leds.red, profile.leds.active_high, false);
    writeLed(profile.leds.green, profile.leds.active_high, false);
    writeLed(profile.leds.blue, profile.leds.active_high, false);

    if (profile.keyboard.interrupt_pin >= 0)
    {
        pinMode(profile.keyboard.interrupt_pin, INPUT_PULLUP);
    }

    if (profile.battery.enable_pin >= 0)
    {
        pinMode(profile.battery.enable_pin, OUTPUT);
        digitalWrite(profile.battery.enable_pin, HIGH);
    }
}

void TEchoLiteBoard::enablePeripheralRail()
{
    if (peripheral_rail_enabled_)
    {
        return;
    }

    const auto& profile = kBoardProfile;
    if (profile.peripheral_3v3_enable >= 0)
    {
        pinMode(profile.peripheral_3v3_enable, OUTPUT);
        digitalWrite(profile.peripheral_3v3_enable, HIGH);
        delay(100);
        digitalWrite(profile.peripheral_3v3_enable, LOW);
        delay(1500);
        digitalWrite(profile.peripheral_3v3_enable, HIGH);
        delay(200);
    }
    peripheral_rail_enabled_ = true;
}

void TEchoLiteBoard::wakeUp()
{
    enablePeripheralRail();
    setStatusLed(false);
}

void TEchoLiteBoard::handlePowerButton()
{
    pulseNotificationLed(40);
}

void TEchoLiteBoard::softwareShutdown()
{
    NVIC_SystemReset();
}

int TEchoLiteBoard::getPowerTier() const
{
    const int level = const_cast<TEchoLiteBoard*>(this)->readBatteryPercent();
    if (level < 0)
    {
        return 0;
    }
    if (level <= 10)
    {
        return 2;
    }
    if (level <= 20)
    {
        return 1;
    }
    return 0;
}

void TEchoLiteBoard::setBrightness(uint8_t level)
{
    brightness_ = static_cast<uint8_t>(
        std::clamp<int>(level, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL));
}

uint8_t TEchoLiteBoard::getBrightness()
{
    return brightness_;
}

bool TEchoLiteBoard::hasKeyboard()
{
    return true;
}

void TEchoLiteBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = static_cast<uint8_t>(
        std::clamp<int>(level, DEVICE_MIN_BRIGHTNESS_LEVEL, DEVICE_MAX_BRIGHTNESS_LEVEL));
    applyKeyboardBacklight();
}

uint8_t TEchoLiteBoard::keyboardGetBrightness()
{
    return keyboard_brightness_;
}

void TEchoLiteBoard::setKeyboardLightEnabled(bool enabled)
{
    keyboard_light_enabled_ = enabled;
    ::boards::t_echo_lite::settings_store::queueSaveKeyboardLightEnabled(enabled);
    applyKeyboardBacklight();
}

bool TEchoLiteBoard::keyboardLightEnabled() const
{
    return keyboard_light_enabled_;
}

void TEchoLiteBoard::setMessageKeyboardLightEnabled(bool enabled)
{
    message_keyboard_light_enabled_ = enabled;
    ::boards::t_echo_lite::settings_store::queueSaveMessageKeyboardLightEnabled(enabled);
}

bool TEchoLiteBoard::messageKeyboardLightEnabled() const
{
    return message_keyboard_light_enabled_;
}

void TEchoLiteBoard::blinkMessageKeyboardLight(uint8_t blink_count)
{
    if (!message_keyboard_light_enabled_ || blink_count == 0)
    {
        return;
    }

    if (!ensureKeyboardBacklightReady())
    {
        return;
    }

    const uint16_t configured_brightness =
        static_cast<uint16_t>((static_cast<uint32_t>(keyboard_brightness_) *
                               kBoardProfile.keyboard_backlight.max_brightness) /
                              DEVICE_MAX_BRIGHTNESS_LEVEL);
    const uint16_t visible_brightness =
        std::max<uint16_t>(configured_brightness,
                           static_cast<uint16_t>(kBoardProfile.keyboard_backlight.max_brightness / 2U));

    for (uint8_t i = 0; i < blink_count; ++i)
    {
        (void)applyAw21009Brightness(*this, visible_brightness);
        delay(90);
        (void)applyAw21009Brightness(*this, 0);
        if (i + 1 < blink_count)
        {
            delay(90);
        }
    }

    applyKeyboardBacklight();
}

bool TEchoLiteBoard::ensureKeyboardBacklightReady()
{
    if (keyboard_backlight_ready_)
    {
        return true;
    }

    enablePeripheralRail();
    delay(10);
    keyboard_backlight_ready_ = configureAw21009(*this);
    return keyboard_backlight_ready_;
}

void TEchoLiteBoard::applyKeyboardBacklight()
{
    if (!keyboard_light_enabled_ && !keyboard_backlight_ready_)
    {
        return;
    }

    if (!ensureKeyboardBacklightReady())
    {
        return;
    }

    const uint16_t brightness = keyboard_light_enabled_
                                    ? static_cast<uint16_t>((static_cast<uint32_t>(keyboard_brightness_) *
                                                             kBoardProfile.keyboard_backlight.max_brightness) /
                                                            DEVICE_MAX_BRIGHTNESS_LEVEL)
                                    : 0U;
    (void)applyAw21009Brightness(*this, brightness);
}

bool TEchoLiteBoard::isRTCReady() const
{
    return gps_runtime_ ? gps_runtime_->isRtcReady() : false;
}

bool TEchoLiteBoard::isCharging()
{
    return false;
}

int TEchoLiteBoard::readBatteryPercent() const
{
    const auto& battery = kBoardProfile.battery;
    if (battery.adc_pin < 0)
    {
        return -1;
    }

    if (battery.enable_pin >= 0)
    {
        pinMode(battery.enable_pin, OUTPUT);
        digitalWrite(battery.enable_pin, HIGH);
        delay(2);
    }
    analogReference(AR_INTERNAL_3_0);
    analogReadResolution(battery.adc_resolution_bits);
    const int raw = analogRead(battery.adc_pin);
    if (raw <= 0)
    {
        return -1;
    }

    const float max_raw = static_cast<float>((1UL << battery.adc_resolution_bits) - 1UL);
    const float voltage = (static_cast<float>(raw) / max_raw) * battery.aref_voltage * battery.adc_multiplier;
    const float ratio = (voltage - 3.30f) / (4.20f - 3.30f);
    const float clamped = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
    return static_cast<int>(clamped * 100.0f + 0.5f);
}

int TEchoLiteBoard::getBatteryLevel()
{
    return readBatteryPercent();
}

bool TEchoLiteBoard::isSDReady() const
{
    return false;
}

bool TEchoLiteBoard::isCardReady()
{
    return false;
}

bool TEchoLiteBoard::isGPSReady() const
{
    return isGpsRuntimeReady();
}

void TEchoLiteBoard::vibrator()
{
    pulseNotificationLed(20, "vibrator");
}

void TEchoLiteBoard::stopVibrator()
{
    const auto& leds = kBoardProfile.leds;
    const int pin = leds.notification_shares_status ? leds.status : leds.notification;
    writeLed(pin, leds.active_high, false);
}

bool TEchoLiteBoard::ensureMessageAudioReady()
{
    if (audio_codec_ready_)
    {
        (void)writeEs8311(*this, kEs8311DacReg32, es8311VolumeRegister(message_tone_volume_));
        return true;
    }

    enablePeripheralRail();
    delay(10);
    audio_codec_ready_ = configureEs8311ForMessageTone(*this, message_tone_volume_);
    return audio_codec_ready_;
}

bool TEchoLiteBoard::playMessageToneChunk(uint16_t word_count)
{
    if (word_count == 0)
    {
        return true;
    }

    const auto& audio = kBoardProfile.audio;
    if (audio.dac_data < 0 || audio.bit_clock < 0 || audio.word_select < 0)
    {
        return false;
    }

    nrf_i2s_disable(NRF_I2S);
    nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_TXPTRUPD);
    nrf_i2s_event_clear(NRF_I2S, NRF_I2S_EVENT_STOPPED);
    nrf_gpio_cfg_output(static_cast<uint32_t>(audio.bit_clock));
    nrf_gpio_cfg_output(static_cast<uint32_t>(audio.word_select));
    if (audio.master_clock >= 0)
    {
        nrf_gpio_cfg_output(static_cast<uint32_t>(audio.master_clock));
    }
    nrf_gpio_cfg_output(static_cast<uint32_t>(audio.dac_data));
    if (audio.adc_data >= 0)
    {
        nrf_gpio_cfg_input(static_cast<uint32_t>(audio.adc_data), NRF_GPIO_PIN_NOPULL);
    }
    nrf_i2s_pins_set(NRF_I2S,
                     static_cast<uint32_t>(audio.bit_clock),
                     static_cast<uint32_t>(audio.word_select),
                     audio.master_clock >= 0 ? static_cast<uint32_t>(audio.master_clock) : NRF_I2S_PIN_NOT_CONNECTED,
                     static_cast<uint32_t>(audio.dac_data),
                     audio.adc_data >= 0 ? static_cast<uint32_t>(audio.adc_data) : NRF_I2S_PIN_NOT_CONNECTED);
#if NRF_I2S_HAS_CLKCONFIG
    nrf_i2s_clk_configure(NRF_I2S, NRF_I2S_CLKSRC_PCLK32M, false);
#endif
    if (!nrf_i2s_configure(NRF_I2S,
                           NRF_I2S_MODE_MASTER,
                           NRF_I2S_FORMAT_I2S,
                           NRF_I2S_ALIGN_LEFT,
                           NRF_I2S_SWIDTH_16BIT,
                           NRF_I2S_CHANNELS_STEREO,
                           NRF_I2S_MCK_32MDIV23,
                           NRF_I2S_RATIO_32X))
    {
        return false;
    }

    nrf_i2s_transfer_set(NRF_I2S, word_count, nullptr, s_message_tone_i2s_buffer);
    nrf_i2s_enable(NRF_I2S);
    nrf_i2s_task_trigger(NRF_I2S, NRF_I2S_TASK_START);
    const uint32_t duration_ms =
        ((static_cast<uint32_t>(word_count) * 1000U) + (kMessageToneSampleRateHz - 1U)) /
        kMessageToneSampleRateHz;
    delay(duration_ms + 8U);
    stopI2s();
    return true;
}

void TEchoLiteBoard::playMessageTone()
{
    if (message_tone_volume_ == 0)
    {
        return;
    }

    const bool audio_ready = ensureMessageAudioReady();
    AdpcmPlaybackState tone_state{};
    while (true)
    {
        const uint16_t word_count = fillPagerNotificationBuffer(tone_state);
        if (word_count == 0)
        {
            break;
        }

        if (audio_ready)
        {
            (void)playMessageToneChunk(word_count);
        }
        else
        {
            const uint32_t duration_ms =
                ((static_cast<uint32_t>(word_count) * 1000U) + (kMessageToneSampleRateHz - 1U)) /
                kMessageToneSampleRateHz;
            delay(duration_ms);
        }
    }
}

void TEchoLiteBoard::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent > 100U ? 100U : volume_percent;
    if (audio_codec_ready_)
    {
        (void)writeEs8311(*this, kEs8311DacReg32, es8311VolumeRegister(message_tone_volume_));
    }
    ::boards::t_echo_lite::settings_store::queueSaveMessageToneVolume(message_tone_volume_);
}

uint8_t TEchoLiteBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void TEchoLiteBoard::setStatusLed(bool on)
{
    writeLedColor(status_led_color_index_, on, "set_status_led");
}

void TEchoLiteBoard::setStatusLedColor(uint8_t color_index)
{
    status_led_color_index_ = static_cast<uint8_t>(color_index % statusLedColorCount());
    ::boards::t_echo_lite::settings_store::queueSaveStatusLedColor(status_led_color_index_);
    writeLedColor(status_led_color_index_, true, "set_status_led_color");
}

uint8_t TEchoLiteBoard::statusLedColor() const
{
    return status_led_color_index_;
}

uint8_t TEchoLiteBoard::statusLedColorCount()
{
    return static_cast<uint8_t>(arrayCount(kLedColors));
}

const char* TEchoLiteBoard::statusLedColorLabel(uint8_t color_index)
{
    return kLedColors[color_index % arrayCount(kLedColors)].label;
}

void TEchoLiteBoard::pulseNotificationLed(uint32_t pulse_ms, const char* source)
{
    const char* reason = source ? source : "pulse_notification";
    writeLedColor(status_led_color_index_, true, reason, pulse_ms);
    delay(pulse_ms);
    writeLedColor(status_led_color_index_, false, reason, pulse_ms);
}

bool TEchoLiteBoard::pollInputSnapshot(BoardInputSnapshot* out_snapshot) const
{
    return input_runtime_ ? input_runtime_->pollSnapshot(out_snapshot) : false;
}

bool TEchoLiteBoard::formatLoraFrequencyMHz(uint32_t freq_hz, char* out, std::size_t out_len) const
{
    if (!out || out_len == 0 || freq_hz == 0)
    {
        return false;
    }

    const uint32_t mhz = freq_hz / 1000000UL;
    const uint32_t khz = (freq_hz % 1000000UL) / 1000UL;
    std::snprintf(out, out_len, "%lu.%03luMHz",
                  static_cast<unsigned long>(mhz),
                  static_cast<unsigned long>(khz));
    return true;
}

uint16_t TEchoLiteBoard::inputDebounceMs() const
{
    return input_runtime_ ? input_runtime_->debounceMs() : kBoardProfile.inputs.debounce_ms;
}

bool TEchoLiteBoard::ensureI2cReady()
{
    if (i2c_initialized_)
    {
        return true;
    }

    const auto& profile = kBoardProfile;
    Wire.setPins(profile.i2c.sda, profile.i2c.scl);
    Wire.begin();
    Wire.setClock(400000);
    i2c_initialized_ = true;
    return true;
}

bool TEchoLiteBoard::lockI2c(uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    while (true)
    {
        noInterrupts();
        if (!i2c_locked_)
        {
            i2c_locked_ = true;
            interrupts();
            return true;
        }
        interrupts();

        if ((millis() - start_ms) >= timeout_ms)
        {
            return false;
        }
        delay(1);
    }
}

void TEchoLiteBoard::unlockI2c()
{
    noInterrupts();
    i2c_locked_ = false;
    interrupts();
}

TwoWire& TEchoLiteBoard::i2cWire()
{
    (void)ensureI2cReady();
    return Wire;
}

bool TEchoLiteBoard::pollInputEvent(BoardInputEvent* out_event)
{
    return input_runtime_ ? input_runtime_->pollEvent(out_event) : false;
}

namespace
{
SPIClass& epaperSpi()
{
    const auto& spi = ::boards::t_echo_lite::kBoardProfile.epaper.spi;
    static SPIClass bus(NRF_SPIM1, spi.miso, spi.sck, spi.mosi);
    return bus;
}

class EpaperMonoDisplay final : public ::ui::mono::MonoDisplay
{
  public:
    EpaperMonoDisplay()
        : display_(::boards::t_echo_lite::kBoardProfile.epaper.width,
                   ::boards::t_echo_lite::kBoardProfile.epaper.height,
                   ::boards::t_echo_lite::kBoardProfile.epaper.dc,
                   ::boards::t_echo_lite::kBoardProfile.epaper.reset,
                   ::boards::t_echo_lite::kBoardProfile.epaper.spi.cs,
                   ::boards::t_echo_lite::kBoardProfile.epaper.sram_cs,
                   ::boards::t_echo_lite::kBoardProfile.epaper.busy,
                   &epaperSpi(),
                   8000000)
    {
    }

    bool begin() override;
    int width() const override { return display_.width(); }
    int height() const override { return display_.height(); }
    void clear() override
    {
        if (online_)
        {
            display_.fillScreen(EPD_WHITE);
            display_.clearBuffer();
            std::memset(frame_bits_, 0, sizeof(frame_bits_));
            dirty_ = true;
        }
    }
    void drawPixel(int x, int y, bool on) override
    {
        if (online_)
        {
            display_.drawPixel(x, y, on ? EPD_BLACK : EPD_WHITE);
            setFramePixel(x, y, on);
            dirty_ = true;
        }
    }
    void drawHLine(int x, int y, int w) override
    {
        if (online_)
        {
            display_.drawFastHLine(x, y, w, EPD_BLACK);
            fillFrameRect(x, y, w, 1, true);
            dirty_ = true;
        }
    }
    void fillRect(int x, int y, int w, int h, bool on) override
    {
        if (online_)
        {
            display_.fillRect(x, y, w, h, on ? EPD_BLACK : EPD_WHITE);
            fillFrameRect(x, y, w, h, on);
            dirty_ = true;
        }
    }
    void present() override
    {
        if (!online_ || !dirty_)
        {
            return;
        }
        const bool full_refresh = full_refresh_pending_;
        if (!full_refresh && std::memcmp(frame_bits_, presented_bits_, sizeof(frame_bits_)) == 0)
        {
            dirty_ = false;
            return;
        }
        const uint32_t now_ms = millis();
        if (last_present_ms_ != 0 && (now_ms - last_present_ms_) < kEpaperPresentMinIntervalMs)
        {
            delay(kEpaperPresentMinIntervalMs - (now_ms - last_present_ms_));
        }

        if (full_refresh)
        {
            display_.display(Adafruit_EPD::Update_Mode::FULL_REFRESH, true);
            full_refresh_pending_ = false;
            partial_refresh_base_map_ready_ = false;
            partial_refresh_count_ = 0;
        }
        else
        {
            if (!partial_refresh_base_map_ready_)
            {
                display_.setRAMValueBaseMap(Adafruit_EPD::Update_Mode::FAST_REFRESH);
                partial_refresh_base_map_ready_ = true;
            }
            display_.display(Adafruit_EPD::Update_Mode::PARTIAL_REFRESH, true);
            if (partial_refresh_count_ < 0xFFU)
            {
                ++partial_refresh_count_;
            }
        }

        std::memcpy(presented_bits_, frame_bits_, sizeof(presented_bits_));
        last_present_ms_ = millis();
        dirty_ = false;
    }
    void onWakeFromSleep() override
    {
        full_refresh_pending_ = true;
        if (online_)
        {
            dirty_ = true;
        }
    }

  private:
    void setFramePixel(int x, int y, bool on)
    {
        const int w = display_.width();
        const int h = display_.height();
        if (x < 0 || y < 0 || x >= w || y >= h)
        {
            return;
        }

        const size_t bit_index = static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x);
        if (bit_index >= static_cast<size_t>(kEpaperMirrorPixels))
        {
            return;
        }
        const uint8_t mask = static_cast<uint8_t>(1U << (bit_index & 0x07U));
        uint8_t& byte = frame_bits_[bit_index >> 3];
        if (on)
        {
            byte = static_cast<uint8_t>(byte | mask);
        }
        else
        {
            byte = static_cast<uint8_t>(byte & ~mask);
        }
    }

    void fillFrameRect(int x, int y, int w, int h, bool on)
    {
        if (w <= 0 || h <= 0)
        {
            return;
        }

        const int display_w = display_.width();
        const int display_h = display_.height();
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(display_w, x + w);
        const int y1 = std::min(display_h, y + h);
        for (int yy = y0; yy < y1; ++yy)
        {
            for (int xx = x0; xx < x1; ++xx)
            {
                setFramePixel(xx, yy, on);
            }
        }
    }

    Adafruit_SSD1681 display_;
    uint8_t frame_bits_[kEpaperMirrorBytes] = {};
    uint8_t presented_bits_[kEpaperMirrorBytes] = {};
    bool initialized_ = false;
    bool online_ = false;
    bool dirty_ = false;
    bool full_refresh_pending_ = false;
    bool partial_refresh_base_map_ready_ = false;
    uint8_t partial_refresh_count_ = 0;
    uint32_t last_present_ms_ = 0;
};

bool EpaperMonoDisplay::begin()
{
    if (initialized_)
    {
        return online_;
    }
    initialized_ = true;

    auto& board = ::boards::t_echo_lite::TEchoLiteBoard::instance();
    (void)board.begin();
    const auto& epaper = ::boards::t_echo_lite::kBoardProfile.epaper;
    if (epaper.bs1 >= 0)
    {
        pinMode(epaper.bs1, OUTPUT);
        digitalWrite(epaper.bs1, LOW);
    }
    epaperSpi().begin();
    display_.begin();
    online_ = true;
    display_.setRotation(1);
    display_.setTextWrap(false);
    display_.fillScreen(EPD_WHITE);
    display_.clearBuffer();
    std::memset(frame_bits_, 0, sizeof(frame_bits_));
    std::memset(presented_bits_, 0, sizeof(presented_bits_));
    full_refresh_pending_ = false;
    partial_refresh_base_map_ready_ = false;
    partial_refresh_count_ = 0;
    last_present_ms_ = 0;
    dirty_ = false;
    return online_;
}

} // namespace

::ui::mono::MonoDisplay& TEchoLiteBoard::monoDisplay()
{
    static EpaperMonoDisplay display;
    return display;
}

TEchoLiteBoard::I2cGuard::I2cGuard(TEchoLiteBoard& board, uint32_t timeout_ms)
    : board_(&board),
      locked_(board.ensureI2cReady() && board.lockI2c(timeout_ms))
{
}

TEchoLiteBoard::I2cGuard::~I2cGuard()
{
    if (board_ && locked_)
    {
        board_->unlockI2c();
    }
}

bool TEchoLiteBoard::I2cGuard::locked() const
{
    return locked_;
}

TEchoLiteBoard::I2cGuard::operator bool() const
{
    return locked_;
}

const char* TEchoLiteBoard::defaultLongName() const
{
    return kBoardProfile.identity.long_name;
}

const char* TEchoLiteBoard::defaultShortName() const
{
    return kBoardProfile.identity.short_name;
}

const char* TEchoLiteBoard::defaultBleName() const
{
    return kBoardProfile.identity.ble_name;
}

bool TEchoLiteBoard::prepareRadioHardware()
{
    if (radio_hw_ready_)
    {
        return true;
    }

    (void)begin();
    enablePeripheralRail();

    const auto& profile = kBoardProfile;
    if (profile.lora.power_en >= 0)
    {
        pinMode(profile.lora.power_en, OUTPUT);
        digitalWrite(profile.lora.power_en, HIGH);
        delay(5);
    }
    if (profile.lora.rf_vc1 >= 0 && profile.lora.rf_vc2 >= 0)
    {
        pinMode(profile.lora.rf_vc1, OUTPUT);
        pinMode(profile.lora.rf_vc2, OUTPUT);
        digitalWrite(profile.lora.rf_vc1, LOW);
        digitalWrite(profile.lora.rf_vc2, HIGH);
    }
    radio_hw_ready_ = true;
    return true;
}

bool TEchoLiteBoard::beginRadioIo()
{
    return ::boards::t_echo_lite::sx1262RadioPacketIo().begin();
}

platform::nrf52::arduino_common::chat::infra::IRadioPacketIo* TEchoLiteBoard::bindRadioIo()
{
    auto& io = ::boards::t_echo_lite::sx1262RadioPacketIo();
    ::platform::nrf52::arduino_common::chat::infra::bindRadioPacketIo(&io);
    return &io;
}

void TEchoLiteBoard::applyRadioConfig(chat::MeshProtocol protocol, const chat::MeshConfig& config)
{
    ::boards::t_echo_lite::sx1262RadioPacketIo().applyConfig(protocol, config);
}

uint32_t TEchoLiteBoard::activeLoraFrequencyHz() const
{
    return ::boards::t_echo_lite::sx1262RadioPacketIo().appliedFrequencyHz();
}

bool TEchoLiteBoard::startGpsRuntime(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->start(config) : false;
}

bool TEchoLiteBoard::beginGps(const app::AppConfig& config)
{
    return gps_runtime_ ? gps_runtime_->begin(config) : false;
}

void TEchoLiteBoard::applyGpsConfig(const app::AppConfig& config)
{
    if (gps_runtime_)
    {
        gps_runtime_->applyConfig(config);
    }
}

void TEchoLiteBoard::tickGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->tick();
    }
}

bool TEchoLiteBoard::isGpsRuntimeReady() const
{
    return gps_runtime_ ? gps_runtime_->isReady() : false;
}

::gps::GpsState TEchoLiteBoard::gpsData() const
{
    return gps_runtime_ ? gps_runtime_->data() : ::gps::GpsState{};
}

bool TEchoLiteBoard::gpsEnabled() const
{
    return gps_runtime_ ? gps_runtime_->enabled() : false;
}

bool TEchoLiteBoard::gpsPowered() const
{
    return gps_runtime_ ? gps_runtime_->powered() : false;
}

uint32_t TEchoLiteBoard::gpsLastMotionMs() const
{
    return gps_runtime_ ? gps_runtime_->lastMotionMs() : 0;
}

bool TEchoLiteBoard::gpsGnssSnapshot(::gps::GnssSatInfo* out,
                                     std::size_t max,
                                     std::size_t* out_count,
                                     ::gps::GnssStatus* status) const
{
    return gps_runtime_ ? gps_runtime_->gnssSnapshot(out, max, out_count, status) : false;
}

void TEchoLiteBoard::setGpsCollectionInterval(uint32_t interval_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCollectionInterval(interval_ms);
    }
}
void TEchoLiteBoard::setGpsEnabled(bool enabled)
{
    if (gps_runtime_)
    {
        gps_runtime_->setEnabled(enabled);
    }
}
void TEchoLiteBoard::setGpsPowerStrategy(uint8_t strategy)
{
    if (gps_runtime_)
    {
        gps_runtime_->setPowerStrategy(strategy);
    }
}
void TEchoLiteBoard::setGpsConfig(uint8_t mode, uint8_t sat_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setConfig(mode, sat_mask);
    }
}
void TEchoLiteBoard::setGpsExternalNmeaConfig(uint8_t output_hz, uint8_t sentence_mask)
{
    if (gps_runtime_)
    {
        gps_runtime_->setExternalNmeaConfig(output_hz, sentence_mask);
    }
}
void TEchoLiteBoard::setGpsMotionIdleTimeout(uint32_t timeout_ms)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionIdleTimeout(timeout_ms);
    }
}
void TEchoLiteBoard::setGpsMotionSensorId(uint8_t sensor_id)
{
    if (gps_runtime_)
    {
        gps_runtime_->setMotionSensorId(sensor_id);
    }
}
void TEchoLiteBoard::suspendGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->suspend();
    }
}

void TEchoLiteBoard::resumeGps()
{
    if (gps_runtime_)
    {
        gps_runtime_->resume();
    }
}
void TEchoLiteBoard::setCurrentEpochSeconds(uint32_t epoch_s)
{
    if (gps_runtime_)
    {
        gps_runtime_->setCurrentEpochSeconds(epoch_s);
    }
}

uint32_t TEchoLiteBoard::currentEpochSeconds() const
{
    return gps_runtime_ ? gps_runtime_->currentEpochSeconds() : 0;
}

} // namespace boards::t_echo_lite

BoardBase& board = ::boards::t_echo_lite::TEchoLiteBoard::instance();
