#if defined(ARDUINO_T_DECK_PRO)

#include "boards/tdeck_pro/tdeck_pro_board.h"
#include "board/sd_utils.h"

#include <Arduino.h>
#include <AudioOutputI2S.h>
#include <SPI.h>
#include <Wire.h>
#include <ctime>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <limits>
#include <new>
#include <sys/time.h>

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "platform/ui/audio/pager_notification_tone.h"
#include "sys/bus_access_scope.h"
#include "sys/clock.h"

namespace boards::tdeck_pro
{

namespace
{
constexpr const char* kTag = "TDeckProBoard";
constexpr time_t kMinValidEpochSeconds = 1577836800; // 2020-01-01 UTC
constexpr uint32_t kEpdSpiHz = 2000000;
constexpr uint32_t kSdSpiHz = 4000000;
constexpr uint8_t kFlushLogLimit = 8;
constexpr uint8_t kEpdRefreshLogLimit = 12;
constexpr uint16_t kEpdPartialAlignment = 8;
constexpr uint32_t kEpdCoalesceDelayMs = 40;
constexpr uint32_t kEpdMinimumRefreshIntervalMs = 750;
constexpr uint32_t kRadioTxMaxTimeoutMs = 120000;
constexpr uint16_t kCst328InfoCommand = 0xD101;
constexpr uint16_t kCst328InfoOffset = 0xD1F4;
constexpr uint16_t kCst328ReportCommand = 0xD000;
constexpr uint32_t kCst328ReportAcknowledgement = 0xD000AB;
constexpr uint8_t kCst328ReportAcknowledgementByte = 0xAB;
sys::runtime::BusAccessToken g_shared_spi_token{};
TaskHandle_t g_shared_spi_task = nullptr;
uint32_t g_shared_spi_depth = 0;

bool cst328Write(TwoWire& wire, uint8_t address, uint32_t value, uint8_t length)
{
    uint8_t buffer[3]{};
    for (uint8_t index = 0; index < length; ++index)
    {
        const uint8_t shift = static_cast<uint8_t>((length - 1U - index) * 8U);
        buffer[index] = static_cast<uint8_t>(value >> shift);
    }

    wire.beginTransmission(address);
    if (wire.write(buffer, length) != length)
    {
        (void)wire.endTransmission(true);
        return false;
    }
    return wire.endTransmission(true) == 0;
}

bool cst328Read(TwoWire& wire, uint8_t address, uint16_t command, uint8_t* output, uint8_t length)
{
    if (output == nullptr || length == 0U || !cst328Write(wire, address, command, 2U))
    {
        return false;
    }

    const size_t received = wire.requestFrom(static_cast<int>(address), static_cast<int>(length), 1);
    if (received != length)
    {
        while (wire.available() > 0)
        {
            (void)wire.read();
        }
        return false;
    }

    for (uint8_t index = 0; index < length; ++index)
    {
        const int value = wire.read();
        if (value < 0)
        {
            return false;
        }
        output[index] = static_cast<uint8_t>(value);
    }
    return true;
}

uint16_t cst328ScaleCoordinate(uint16_t raw, uint16_t raw_extent, uint16_t logical_extent)
{
    if (logical_extent <= 1U)
    {
        return 0U;
    }
    if (raw_extent <= 1U)
    {
        return raw >= logical_extent ? static_cast<uint16_t>(logical_extent - 1U) : raw;
    }

    const uint16_t raw_last = static_cast<uint16_t>(raw_extent - 1U);
    const uint16_t bounded = raw > raw_last ? raw_last : raw;
    return static_cast<uint16_t>((static_cast<uint32_t>(bounded) * (logical_extent - 1U)) / raw_last);
}

uint32_t radioTxTimeoutMs(SX1262Access& radio, size_t len)
{
    const uint64_t air_us = static_cast<uint64_t>(radio.getTimeOnAir(len));
    uint64_t timeout_ms = 10ULL + ((air_us * 5ULL) + 999ULL) / 1000ULL;
    if (timeout_ms < 100ULL)
    {
        timeout_ms = 100ULL;
    }
    if (timeout_ms > kRadioTxMaxTimeoutMs)
    {
        timeout_ms = kRadioTxMaxTimeoutMs;
    }
    return static_cast<uint32_t>(timeout_ms);
}

void sharedSpiReleaseAllCs()
{
    digitalWrite(TDeckProBoard::profile().lora.cs, HIGH);
    digitalWrite(TDeckProBoard::profile().sd.cs, HIGH);
    digitalWrite(TDeckProBoard::profile().epd.cs, HIGH);
}

void sharedSpiBusInit()
{
    sharedSpiReleaseAllCs();
}

bool sharedSpiLock(
    sys::runtime::BusAccessPolicy policy =
        sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
    const char* owner = "tdeck_pro_spi",
    uint32_t wait_ms = 200U)
{
    sys::runtime::BusAcquireRequest request{};
    request.resource =
        ::platform::esp::common::SharedSpiCoordinator::kSharedBusResource;
    request.policy = policy;
    request.command_id = 0x54445053U;
    request.origin = request.command_id;
    request.deadline_ms = sys::millis_now() + wait_ms;
    request.owner_label = owner;
    const sys::runtime::BusAcquireResult result =
        ::platform::esp::common::shared_spi_coordinator().acquire(request);
    if (result.status != sys::runtime::BusAcquireStatus::Acquired ||
        !result.token.valid)
    {
        return false;
    }
    g_shared_spi_token = result.token;
    g_shared_spi_task = xTaskGetCurrentTaskHandle();
    g_shared_spi_depth = result.token.depth;
    return true;
}

void sharedSpiUnlock()
{
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (!g_shared_spi_token.valid || g_shared_spi_task != current)
    {
        // This board wrapper keeps legacy paired lock/unlock call sites, but
        // the global coordinator remains the authority for ownership errors.
        // Forward an invalid or cross-task release so it is counted and logged
        // instead of being silently hidden by the compatibility bridge.
        ::platform::esp::common::shared_spi_coordinator().release(g_shared_spi_token);
        return;
    }
    sys::runtime::BusAccessToken token = g_shared_spi_token;
    token.depth = g_shared_spi_depth;
    const bool final_release = g_shared_spi_depth <= 1U;
    if (final_release)
    {
        g_shared_spi_token = {};
        g_shared_spi_task = nullptr;
        g_shared_spi_depth = 0;
    }
    else
    {
        --g_shared_spi_depth;
        g_shared_spi_token.depth = g_shared_spi_depth;
    }
    sharedSpiReleaseAllCs();
    ::platform::esp::common::shared_spi_coordinator().release(token);
}

void sharedSpiPrepareDevice(int cs_pin)
{
    sharedSpiReleaseAllCs();
    if (cs_pin >= 0)
    {
        digitalWrite(cs_pin, HIGH);
    }
}

int batteryPercentFromMv(int mv)
{
    if (mv <= 0)
    {
        return -1;
    }
    int pct = static_cast<int>(((mv - 3300) / 900.0f) * 100.0f);
    if (pct < 0)
    {
        pct = 0;
    }
    if (pct > 100)
    {
        pct = 100;
    }
    return pct;
}

bool isLeapYear(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

uint8_t daysInMonth(int year, uint8_t month)
{
    static constexpr uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
    {
        return 0;
    }
    if (month == 2 && isLeapYear(year))
    {
        return 29;
    }
    return kDays[month - 1];
}

bool gpsDatetimeValid(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
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
    return hour < 24 && minute < 60 && second < 60;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

time_t gpsDatetimeToEpochUtc(int year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    const int64_t days = daysFromCivil(year, month, day);
    const int64_t sec_of_day = static_cast<int64_t>(hour) * 3600 +
                               static_cast<int64_t>(minute) * 60 +
                               static_cast<int64_t>(second);
    const int64_t epoch64 = days * 86400 + sec_of_day;
    if (epoch64 < 0 || epoch64 > static_cast<int64_t>(std::numeric_limits<time_t>::max()))
    {
        return static_cast<time_t>(-1);
    }
    return static_cast<time_t>(epoch64);
}

void applyTxPower(SX1262Access& radio, int8_t tx_power)
{
    constexpr int8_t kTxPowerMinDbm = -9;
    int8_t clipped = tx_power;
    if (clipped < kTxPowerMinDbm)
    {
        clipped = kTxPowerMinDbm;
    }
    radio.setOutputPower(clipped);
}
} // namespace

TDeckProBoard::TDeckProBoard()
    : LilyGo_Display(SPI_DRIVER, false)
{
}

TDeckProBoard* TDeckProBoard::getInstance()
{
    // T-Deck and T-LoRa Pager keep their board objects out of scarce internal
    // DRAM. The Pro has the same 8 MiB PSRAM contract, and this object
    // contains several driver instances, so keep the policy aligned with
    // those sibling targets. Its retained EPD surface is separately strict
    // PSRAM-backed below.
    static TDeckProBoard* instance = []() -> TDeckProBoard*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(TDeckProBoard),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(TDeckProBoard));
        }
        return new (storage) TDeckProBoard();
    }();
    return instance;
}

void TDeckProBoard::initSharedPins()
{
    pinMode(profile().lora.enable, OUTPUT);
    digitalWrite(profile().lora.enable, HIGH);

    pinMode(profile().gps.enable, OUTPUT);
    digitalWrite(profile().gps.enable, HIGH);

    pinMode(profile().motion.enable_1v8, OUTPUT);
    digitalWrite(profile().motion.enable_1v8, HIGH);

    if (profile().optional.has_a7682e && profile().optional.a7682e_enable >= 0)
    {
        pinMode(profile().optional.a7682e_enable, OUTPUT);
        digitalWrite(profile().optional.a7682e_enable, HIGH);
    }
    if (profile().optional.has_a7682e && profile().optional.a7682e_pwrkey >= 0)
    {
        pinMode(profile().optional.a7682e_pwrkey, OUTPUT);
        digitalWrite(profile().optional.a7682e_pwrkey, HIGH);
    }
    if (profile().motor_pin >= 0)
    {
        pinMode(profile().motor_pin, OUTPUT);
        digitalWrite(profile().motor_pin, LOW);
    }

    pinMode(profile().sd.cs, OUTPUT);
    digitalWrite(profile().sd.cs, HIGH);
    pinMode(profile().epd.cs, OUTPUT);
    digitalWrite(profile().epd.cs, HIGH);
    pinMode(profile().lora.cs, OUTPUT);
    digitalWrite(profile().lora.cs, HIGH);
    sharedSpiBusInit();
}

bool TDeckProBoard::initPower()
{
    Wire.begin(profile().i2c.sda, profile().i2c.scl);
    delay(10);

    power_ready_ = pmu_.begin(Wire, 0x6B, profile().i2c.sda, profile().i2c.scl);
    battery_gauge_ready_ = gauge_.begin(Wire, profile().i2c.sda, profile().i2c.scl);
    if (power_ready_)
    {
        pmu_.enableMeasure();
        pmu_.disableOTG();
    }
    Serial.printf("[%s] power init charger=%d gauge=%d\n", kTag, power_ready_ ? 1 : 0, battery_gauge_ready_ ? 1 : 0);
    return power_ready_ || battery_gauge_ready_;
}

bool TDeckProBoard::initDisplay()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                       "tdeck_pro_epd_init",
                       45U))
    {
        return false;
    }
    sharedSpiPrepareDevice(profile().epd.cs);
    SPI.begin(profile().spi.sck, profile().spi.miso, profile().spi.mosi, profile().epd.cs);
    epd_.epd2.selectSPI(SPI, SPISettings(kEpdSpiHz, MSBFIRST, SPI_MODE0));
    epd_.init(0, true, 2, false);
    sharedSpiUnlock();

    epd_.setRotation(rotation_);
    epd_.setFullWindow();
    epd_.firstPage();
    bool next_page = true;
    do
    {
        epd_.fillScreen(GxEPD_WHITE);
        if (!sharedSpiLock(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                           "tdeck_pro_epd_init_page",
                           45U))
        {
            return false;
        }
        sharedSpiPrepareDevice(profile().epd.cs);
        next_page = epd_.nextPage();
        sharedSpiUnlock();
    } while (next_page);

    display_ready_ = true;
    Serial.printf("[%s] epd init ok %ux%u spi=%luHz\n", kTag, width(), height(), (unsigned long)kEpdSpiHz);
    return true;
}

bool TDeckProBoard::initTouch()
{
    // The T-Deck Pro uses Hynitron's CST328 at 0x1A. SensorLib's
    // TouchDrvCSTXXX does not include a CST328 transport, so probing it as a
    // CST226/CST816/CST92xx leaves touch_ready_ false and suppresses LVGL's
    // pointer input device. This follows the CST3xx sequence supplied in the
    // board vendor's Hynitron example: hardware reset, D101/D1F4 info probe,
    // then D109 normal mode.
    pinMode(profile().touch.rst, OUTPUT);
    digitalWrite(profile().touch.rst, LOW);
    delay(10);
    digitalWrite(profile().touch.rst, HIGH);
    delay(50);

    uint8_t info[28]{};
    touch_ready_ = cst328Write(Wire, profile().touch.i2c_addr, kCst328InfoCommand, 2U);
    if (touch_ready_)
    {
        delay(1);
        touch_ready_ = cst328Read(Wire, profile().touch.i2c_addr, kCst328InfoOffset, info, sizeof(info));
    }
    if (touch_ready_)
    {
        touch_raw_width_ = static_cast<uint16_t>(static_cast<uint16_t>(info[5]) << 8U | info[4]);
        touch_raw_height_ = static_cast<uint16_t>(static_cast<uint16_t>(info[7]) << 8U | info[6]);
        touch_ready_ = touch_raw_width_ > 1U && touch_raw_height_ > 1U &&
                       cst328Write(Wire, profile().touch.i2c_addr, 0xD109, 2U);
    }
    if (touch_ready_)
    {
        Serial.printf("[DEBUG-touch-a7682e] init model=CST328 raw=%ux%u logical=%dx%d\n",
                      static_cast<unsigned>(touch_raw_width_),
                      static_cast<unsigned>(touch_raw_height_),
                      profile().screen_width,
                      profile().screen_height);
    }
    Serial.printf("[%s] touch init %s\n", kTag, touch_ready_ ? "ok" : "fail");
    return touch_ready_;
}

bool TDeckProBoard::initKeyboard()
{
    keyboard_ready_ = keyboard_.begin(profile().keyboard.i2c_addr, &Wire);
    if (!keyboard_ready_)
    {
        Serial.printf("[%s] keyboard init fail\n", kTag);
        return false;
    }
    keyboard_.matrix(profile().keyboard.rows, profile().keyboard.cols);
    keyboard_.flush();
    keyboardSetBrightness(keyboard_brightness_);
    Serial.printf("[%s] keyboard init ok\n", kTag);
    return true;
}

bool TDeckProBoard::initMotion()
{
    motion_ready_ = motion_.begin(Wire, profile().motion.i2c_addr, profile().i2c.sda, profile().i2c.scl);
    Serial.printf("[%s] motion init %s\n", kTag, motion_ready_ ? "ok" : "fail");
    return motion_ready_;
}

bool TDeckProBoard::initRadio()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_init",
                       200U))
    {
        return false;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    SPI.begin(profile().spi.sck, profile().spi.miso, profile().spi.mosi, profile().lora.cs);
    radio_.reset();
    radio_ready_ = (radio_.begin() == RADIOLIB_ERR_NONE);
    sharedSpiUnlock();
    Serial.printf("[%s] radio init %s\n", kTag, radio_ready_ ? "ok" : "fail");
    return radio_ready_;
}

bool TDeckProBoard::initStorage()
{
    sd_ready_ = installSD();
    Serial.printf("[%s] sd init %s\n", kTag, sd_ready_ ? "ok" : "fail");
    return sd_ready_;
}

bool TDeckProBoard::installSD()
{
    if (!::platform::esp::boards::storageStartupGateSatisfied())
    {
        Serial.printf("[%s] SD mount deferred: first shared-SPI display transaction incomplete\n", kTag);
        return false;
    }

    static const int extra_cs_pins[] = {
        profile().lora.cs,
        profile().epd.cs,
    };
    static const ::platform::esp::arduino_common::storage::SdSpiBusConfig
        kSharedSpiBus{SPI,
                      profile().spi.sck,
                      profile().spi.miso,
                      profile().spi.mosi};
    uint8_t card_type = sdutil::kCardNone;
    uint32_t card_size_mb = 0;
    const bool ok = sdutil::installSpiSd(
        profile().sd.cs,
        kSdSpiHz,
        "/sd",
        extra_cs_pins,
        sizeof(extra_cs_pins) / sizeof(extra_cs_pins[0]),
        &card_type,
        &card_size_mb,
        8,
        kSharedSpiBus);
    if (ok)
    {
        Serial.printf("[%s] SD card type=%u size=%luMB\n",
                      kTag,
                      static_cast<unsigned>(card_type),
                      static_cast<unsigned long>(card_size_mb));
    }
    return ok;
}

bool TDeckProBoard::ensureSDReady()
{
    if (isCardReady())
    {
        sd_ready_ = true;
        return true;
    }
    return initStorage();
}

void TDeckProBoard::uninstallSD()
{
    ::platform::esp::arduino_common::storage::unmount_sd_card();
}

uint32_t TDeckProBoard::begin(uint32_t disable_hw_init)
{
    Serial.begin(115200);
    delay(30);
    Serial.printf("[%s] begin variant=%s\n", kTag,
#if defined(TRAIL_MATE_TDECK_PRO_A7682E)
                  "a7682e"
#else
                  "pcm512a"
#endif
    );

    // The board singleton is constructed before setup(), when strict PSRAM
    // allocations are not yet available. Allocate this retained EPD surface
    // only after the Arduino/ESP runtime has initialized external RAM.
    if (mono_buffer_.empty())
    {
        mono_buffer_.resize(static_cast<size_t>(profile().screen_width * profile().screen_height) / 8U, 0xFF);
    }

    initSharedPins();
    (void)initPower();
    (void)initDisplay();

    if ((disable_hw_init & NO_HW_TOUCH) == 0)
    {
        (void)initTouch();
    }
    (void)initKeyboard();
    (void)initMotion();
    if ((disable_hw_init & NO_HW_SD) == 0)
    {
        (void)initStorage();
    }
    if ((disable_hw_init & NO_HW_GPS) == 0)
    {
        (void)initGPS();
    }
    (void)initRadio();

    rtc_ready_ = time(nullptr) >= kMinValidEpochSeconds;

    uint32_t probe = 0;
    if (power_ready_) probe |= HW_PMU_ONLINE;
    if (battery_gauge_ready_) probe |= HW_GAUGE_ONLINE;
    if (touch_ready_) probe |= HW_TOUCH_ONLINE;
    if (keyboard_ready_) probe |= HW_KEYBOARD_ONLINE;
    if (motion_ready_) probe |= HW_BHI260AP_ONLINE;
    if (sd_ready_) probe |= HW_SD_ONLINE;
    if (gps_ready_) probe |= HW_GPS_ONLINE;
    if (radio_ready_) probe |= HW_RADIO_ONLINE;
    return probe;
}

void TDeckProBoard::setBrightness(uint8_t level)
{
    brightness_ = level;
}

void TDeckProBoard::keyboardSetBrightness(uint8_t level)
{
    keyboard_brightness_ = level;
    if (profile().keyboard.led >= 0)
    {
        analogWrite(profile().keyboard.led, level);
    }
}

bool TDeckProBoard::isRTCReady() const
{
    return rtc_ready_ || (time(nullptr) >= kMinValidEpochSeconds);
}

bool TDeckProBoard::isCharging()
{
    return power_ready_ ? pmu_.isCharging() : false;
}

int TDeckProBoard::getBatteryLevel()
{
    if (battery_gauge_ready_)
    {
        int level = static_cast<int>(gauge_.getStateOfCharge());
        if (level >= 0 && level <= 100)
        {
            last_battery_level_ = level;
            return level;
        }
    }
    if (power_ready_)
    {
        int level = batteryPercentFromMv(static_cast<int>(pmu_.getBattVoltage()));
        if (level >= 0 && level <= 100)
        {
            last_battery_level_ = level;
            return level;
        }
    }
    return last_battery_level_;
}

bool TDeckProBoard::isCardReady()
{
    return sd_ready_ && ::platform::esp::arduino_common::storage::sd_card_ready();
}

void TDeckProBoard::vibrator()
{
    if (profile().motor_pin >= 0)
    {
        digitalWrite(profile().motor_pin, HIGH);
    }
}

void TDeckProBoard::stopVibrator()
{
    if (profile().motor_pin >= 0)
    {
        digitalWrite(profile().motor_pin, LOW);
    }
}

void TDeckProBoard::playMessageTone()
{
    if (!profile().optional.has_pcm512a || message_tone_volume_ == 0)
    {
        return;
    }

    static constexpr size_t kFramesPerChunk = 128;
    namespace pager_tone = ::platform::ui::audio::pager_notification;

    AudioOutputI2S audio_out(1, AudioOutputI2S::EXTERNAL_I2S);
    audio_out.SetPinout(profile().optional.i2s_bclk,
                        profile().optional.i2s_lrc,
                        profile().optional.i2s_dout);
    audio_out.SetRate(pager_tone::kPlaybackSampleRateHz);
    audio_out.SetBitsPerSample(16);
    audio_out.SetChannels(pager_tone::kChannels);
    float gain = static_cast<float>(message_tone_volume_) / 250.0f;
    if (gain > 0.40f)
    {
        gain = 0.40f;
    }
    audio_out.SetGain(gain);

    if (audio_out.begin())
    {
        const uint32_t deadline = millis() + 1600U;
        int16_t pcm[kFramesPerChunk * pager_tone::kChannels];
        pager_tone::AdpcmPlaybackState tone_state{};
        while (pager_tone::hasMore(tone_state) && millis() < deadline)
        {
            const uint16_t frames = pager_tone::fillStereoInterleaved(
                tone_state, pcm, static_cast<uint16_t>(kFramesPerChunk));
            if (frames == 0U)
            {
                break;
            }

            uint16_t written = 0U;
            while (written < frames && millis() < deadline)
            {
                const uint16_t consumed =
                    audio_out.ConsumeSamples(&pcm[written * pager_tone::kChannels],
                                             static_cast<uint16_t>(frames - written));
                if (consumed == 0U)
                {
                    delay(1);
                    continue;
                }
                written = static_cast<uint16_t>(written + consumed);
            }
        }
        audio_out.flush();
    }
    audio_out.stop();
}

void TDeckProBoard::setMessageToneVolume(uint8_t volume_percent)
{
    if (volume_percent > 100)
    {
        volume_percent = 100;
    }
    message_tone_volume_ = volume_percent;
}

uint8_t TDeckProBoard::getMessageToneVolume() const
{
    return message_tone_volume_;
}

void TDeckProBoard::setRotation(uint8_t rotation)
{
    const uint8_t next_rotation = rotation & 0x3;
    if (rotation_ != next_rotation)
    {
        // A controller window is expressed in the active rotation. The next
        // copied LVGL frame must therefore establish a new full-screen base.
        epd_force_full_refresh_ = true;
    }
    rotation_ = next_rotation;
    epd_.setRotation(rotation_);
}

uint16_t TDeckProBoard::width()
{
    return rotation_ % 2 == 0 ? static_cast<uint16_t>(profile().screen_width)
                              : static_cast<uint16_t>(profile().screen_height);
}

uint16_t TDeckProBoard::height()
{
    return rotation_ % 2 == 0 ? static_cast<uint16_t>(profile().screen_height)
                              : static_cast<uint16_t>(profile().screen_width);
}

bool TDeckProBoard::setBit(int16_t x, int16_t y, bool black)
{
    if (x < 0 || y < 0 || x >= profile().screen_width || y >= profile().screen_height)
    {
        return false;
    }
    const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(profile().screen_width) + static_cast<size_t>(x);
    const size_t byte_idx = idx / 8U;
    const uint8_t bit_mask = static_cast<uint8_t>(0x80U >> (idx % 8U));
    // The retained GxEPD2 bitmap uses a cleared bit for black because it is
    // drawn through drawInvertedBitmap().
    const bool current_black = (mono_buffer_[byte_idx] & bit_mask) == 0U;
    if (current_black == black)
    {
        return false;
    }
    if (black)
    {
        mono_buffer_[byte_idx] &= static_cast<uint8_t>(~bit_mask);
    }
    else
    {
        mono_buffer_[byte_idx] |= bit_mask;
    }
    return true;
}

void TDeckProBoard::mergeDirtyRegion(uint16_t x,
                                     uint16_t y,
                                     uint16_t region_width,
                                     uint16_t region_height,
                                     uint32_t now_ms)
{
    const uint16_t screen_width = static_cast<uint16_t>(profile().screen_width);
    const uint16_t screen_height = static_cast<uint16_t>(profile().screen_height);
    if (region_width == 0U || region_height == 0U || x >= screen_width || y >= screen_height)
    {
        return;
    }

    const uint32_t requested_x2 = static_cast<uint32_t>(x) + region_width - 1U;
    const uint32_t requested_y2 = static_cast<uint32_t>(y) + region_height - 1U;
    const uint16_t x2 = static_cast<uint16_t>(
        requested_x2 < screen_width ? requested_x2 : static_cast<uint32_t>(screen_width - 1U));
    const uint16_t y2 = static_cast<uint16_t>(
        requested_y2 < screen_height ? requested_y2 : static_cast<uint32_t>(screen_height - 1U));

    if (!dirty_region_pending_)
    {
        dirty_x1_ = x;
        dirty_y1_ = y;
        dirty_x2_ = x2;
        dirty_y2_ = y2;
        dirty_since_ms_ = now_ms;
        dirty_region_pending_ = true;
        return;
    }

    if (x < dirty_x1_)
    {
        dirty_x1_ = x;
    }
    if (y < dirty_y1_)
    {
        dirty_y1_ = y;
    }
    if (x2 > dirty_x2_)
    {
        dirty_x2_ = x2;
    }
    if (y2 > dirty_y2_)
    {
        dirty_y2_ = y2;
    }
}

void TDeckProBoard::clearDirtyRegion()
{
    dirty_region_pending_ = false;
    dirty_since_ms_ = 0;
}

DisplayTransferResult TDeckProBoard::renderEpd(bool full_refresh)
{
    if (!display_ready_)
    {
        return DisplayTransferResult::Unavailable;
    }

    epd_.setRotation(rotation_);
    if (full_refresh)
    {
        epd_.setFullWindow();
    }
    else
    {
        uint16_t x1 = dirty_x1_;
        uint16_t y1 = dirty_y1_;
        uint16_t x2 = dirty_x2_;
        uint16_t y2 = dirty_y2_;
        const uint16_t screen_width = static_cast<uint16_t>(profile().screen_width);
        const uint16_t screen_height = static_cast<uint16_t>(profile().screen_height);

        if ((rotation_ & 0x1U) == 0U)
        {
            x1 = static_cast<uint16_t>(x1 - (x1 % kEpdPartialAlignment));
            const uint16_t end = static_cast<uint16_t>(x2 + 1U);
            x2 = static_cast<uint16_t>(
                ((end + kEpdPartialAlignment - 1U) / kEpdPartialAlignment) * kEpdPartialAlignment - 1U);
            if (x2 >= screen_width)
            {
                x2 = static_cast<uint16_t>(screen_width - 1U);
            }
        }
        else
        {
            y1 = static_cast<uint16_t>(y1 - (y1 % kEpdPartialAlignment));
            const uint16_t end = static_cast<uint16_t>(y2 + 1U);
            y2 = static_cast<uint16_t>(
                ((end + kEpdPartialAlignment - 1U) / kEpdPartialAlignment) * kEpdPartialAlignment - 1U);
            if (y2 >= screen_height)
            {
                y2 = static_cast<uint16_t>(screen_height - 1U);
            }
        }

        epd_.setPartialWindow(x1,
                              y1,
                              static_cast<uint16_t>(x2 - x1 + 1U),
                              static_cast<uint16_t>(y2 - y1 + 1U));
    }
    epd_.firstPage();
    bool next_page = true;
    do
    {
        // This only rasterizes into GxEPD2's persistent page surface. Keep the
        // shared controller available until the following nextPage() commits
        // that page to the physical EPD.
        epd_.drawInvertedBitmap(0, 0, mono_buffer_.data(), profile().screen_width, profile().screen_height, GxEPD_BLACK);
        if (!sharedSpiLock(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                           "tdeck_pro_epd_page",
                           45U))
        {
            return DisplayTransferResult::Busy;
        }
        sharedSpiPrepareDevice(profile().epd.cs);
        next_page = epd_.nextPage();
        sharedSpiUnlock();
    } while (next_page);

    return DisplayTransferResult::Completed;
}

DisplayTransferResult TDeckProBoard::servicePendingEpd(uint32_t now_ms, bool force_now)
{
    if (!dirty_region_pending_)
    {
        return DisplayTransferResult::Completed;
    }

    if (!force_now)
    {
        if (now_ms - dirty_since_ms_ < kEpdCoalesceDelayMs)
        {
            return DisplayTransferResult::Busy;
        }
        if (!epd_first_frame_pending_ && now_ms - last_epd_refresh_ms_ < kEpdMinimumRefreshIntervalMs)
        {
            return DisplayTransferResult::Busy;
        }
    }

    const uint32_t dirty_width = static_cast<uint32_t>(dirty_x2_ - dirty_x1_ + 1U);
    const uint32_t dirty_height = static_cast<uint32_t>(dirty_y2_ - dirty_y1_ + 1U);
    // A full waveform is a visual lifecycle decision, not a heuristic based
    // on changed area or a count of cursor moves.  Startup/rotation establish
    // a panel baseline and the text UI explicitly requests one on page entry
    // and exit; ordinary selection and content updates remain partial.
    const bool full_refresh = epd_force_full_refresh_;

    const DisplayTransferResult result = renderEpd(full_refresh);
    if (result != DisplayTransferResult::Completed)
    {
        return result;
    }

    last_epd_refresh_ms_ = sys::millis_now();
    epd_first_frame_pending_ = false;
    if (full_refresh)
    {
        epd_force_full_refresh_ = false;
    }

    static uint8_t s_epd_refresh_log_count = 0;
    if (s_epd_refresh_log_count < kEpdRefreshLogLimit)
    {
        Serial.printf("[%s] epd refresh #%u mode=%s merged=(%u,%u %lux%lu)\n",
                      kTag,
                      static_cast<unsigned>(s_epd_refresh_log_count + 1U),
                      full_refresh ? "full" : "partial",
                      static_cast<unsigned>(dirty_x1_),
                      static_cast<unsigned>(dirty_y1_),
                      static_cast<unsigned long>(dirty_width),
                      static_cast<unsigned long>(dirty_height));
        ++s_epd_refresh_log_count;
    }
    clearDirtyRegion();
    return DisplayTransferResult::Completed;
}

void TDeckProBoard::serviceDisplay(uint32_t now_ms)
{
    (void)servicePendingEpd(now_ms, false);
}

void TDeckProBoard::requestFullRefresh()
{
    epd_force_full_refresh_ = true;
}

void TDeckProBoard::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    (void)transferPixels(x1, y1, x2, y2, color);
}

DisplayTransferResult TDeckProBoard::transferPixels(uint16_t x1,
                                                    uint16_t y1,
                                                    uint16_t x2,
                                                    uint16_t y2,
                                                    uint16_t* color)
{
    if (!color)
    {
        return DisplayTransferResult::Failed;
    }

    const uint16_t screen_width = static_cast<uint16_t>(profile().screen_width);
    const uint16_t screen_height = static_cast<uint16_t>(profile().screen_height);
    if (x1 >= screen_width || y1 >= screen_height || x2 == 0U || y2 == 0U)
    {
        return DisplayTransferResult::Failed;
    }

    const uint16_t copy_width = static_cast<uint16_t>(
        static_cast<uint32_t>(x1) + x2 <= screen_width ? x2 : screen_width - x1);
    const uint16_t copy_height = static_cast<uint16_t>(
        static_cast<uint32_t>(y1) + y2 <= screen_height ? y2 : screen_height - y1);

    static uint8_t s_flush_log_count = 0;
    uint32_t dark_pixels = 0;
    bool has_changed_pixels = false;
    uint16_t changed_x1 = copy_width;
    uint16_t changed_y1 = copy_height;
    uint16_t changed_x2 = 0;
    uint16_t changed_y2 = 0;

    for (uint16_t row = 0; row < copy_height; ++row)
    {
        for (uint16_t col = 0; col < copy_width; ++col)
        {
            const uint16_t pixel = color[static_cast<size_t>(row) * x2 + col];
            const uint8_t r = static_cast<uint8_t>((pixel >> 11) & 0x1F);
            const uint8_t g = static_cast<uint8_t>((pixel >> 5) & 0x3F);
            const uint8_t b = static_cast<uint8_t>(pixel & 0x1F);
            const uint16_t luminance = static_cast<uint16_t>(r * 299U + g * 587U + b * 114U);
            const bool black = luminance < 16384U;
            if (black)
            {
                dark_pixels++;
            }
            if (setBit(static_cast<int16_t>(x1 + col), static_cast<int16_t>(y1 + row), black))
            {
                has_changed_pixels = true;
                if (col < changed_x1)
                {
                    changed_x1 = col;
                }
                if (row < changed_y1)
                {
                    changed_y1 = row;
                }
                if (col > changed_x2)
                {
                    changed_x2 = col;
                }
                if (row > changed_y2)
                {
                    changed_y2 = row;
                }
            }
        }
    }
    if (s_flush_log_count < kFlushLogLimit)
    {
        Serial.printf("[%s] flush #%u area=(%u,%u %ux%u) dark=%lu/%lu\n",
                      kTag,
                      static_cast<unsigned>(s_flush_log_count + 1),
                      static_cast<unsigned>(x1),
                      static_cast<unsigned>(y1),
                      static_cast<unsigned>(copy_width),
                      static_cast<unsigned>(copy_height),
                      static_cast<unsigned long>(dark_pixels),
                      static_cast<unsigned long>(static_cast<uint32_t>(copy_width) * copy_height));
        s_flush_log_count++;
    }

    if (has_changed_pixels)
    {
        mergeDirtyRegion(static_cast<uint16_t>(x1 + changed_x1),
                         static_cast<uint16_t>(y1 + changed_y1),
                         static_cast<uint16_t>(changed_x2 - changed_x1 + 1U),
                         static_cast<uint16_t>(changed_y2 - changed_y1 + 1U),
                         sys::millis_now());
    }
    else if (epd_first_frame_pending_)
    {
        // A white first LVGL frame still needs a physical full refresh to
        // establish the panel/controller baseline and release storage startup.
        mergeDirtyRegion(x1, y1, copy_width, copy_height, sys::millis_now());
    }
    else
    {
        return DisplayTransferResult::Completed;
    }

    // LVGL owns its RGB565 buffer only until this method returns. The first
    // frame remains synchronous so the shared-SPI storage startup gate still
    // observes a real EPD transaction. Later frames are already copied into
    // mono_buffer_ and may be coalesced by serviceDisplay().
    if (epd_first_frame_pending_)
    {
        return servicePendingEpd(sys::millis_now(), true);
    }
    return DisplayTransferResult::Completed;
}

uint8_t TDeckProBoard::getPoint(int16_t* x, int16_t* y, uint8_t get_point)
{
    if (!touch_ready_)
    {
        return 0;
    }
    if (!x || !y || get_point == 0U)
    {
        return 0;
    }

    uint8_t report[7]{};
    const bool report_read = cst328Read(Wire, profile().touch.i2c_addr, kCst328ReportCommand, report, sizeof(report));
    // Each normal-mode CST328 report must be acknowledged, including an
    // empty or malformed one, otherwise the controller stops producing new
    // touch frames.
    (void)cst328Write(Wire, profile().touch.i2c_addr, kCst328ReportAcknowledgement, 3U);
    if (!report_read || report[6] != kCst328ReportAcknowledgementByte ||
        report[0] == kCst328ReportAcknowledgementByte || (report[5] & 0x80U) != 0U ||
        (report[5] & 0x7FU) == 0U)
    {
        return 0;
    }

    const uint16_t raw_x = static_cast<uint16_t>((static_cast<uint16_t>(report[1]) << 4U) |
                                                 ((report[3] >> 4U) & 0x0FU));
    const uint16_t raw_y = static_cast<uint16_t>((static_cast<uint16_t>(report[2]) << 4U) |
                                                 (report[3] & 0x0FU));
    const int16_t tx = static_cast<int16_t>(cst328ScaleCoordinate(raw_x,
                                                                  touch_raw_width_,
                                                                  static_cast<uint16_t>(profile().screen_width)));
    const int16_t ty = static_cast<int16_t>(cst328ScaleCoordinate(raw_y,
                                                                  touch_raw_height_,
                                                                  static_cast<uint16_t>(profile().screen_height)));

    switch (rotation_ & 0x3)
    {
    case 1:
        *x = ty;
        *y = static_cast<int16_t>(profile().screen_width - 1 - tx);
        break;
    case 2:
        *x = static_cast<int16_t>(profile().screen_width - 1 - tx);
        *y = static_cast<int16_t>(profile().screen_height - 1 - ty);
        break;
    case 3:
        *x = static_cast<int16_t>(profile().screen_height - 1 - ty);
        *y = tx;
        break;
    default:
        *x = tx;
        *y = ty;
        break;
    }
    return 1;
}

bool TDeckProBoard::keyEventToChar(uint8_t event, char* c, bool* pressed)
{
    return keyboard_decoder_.decode(event, c, pressed);
}

int TDeckProBoard::getKeyChar(char* c)
{
    if (!keyboard_ready_ || !c || keyboard_.available() == 0)
    {
        return -1;
    }
    bool pressed = false;
    if (!keyEventToChar(keyboard_.getEvent(), c, &pressed))
    {
        return -1;
    }
    return pressed ? KEYBOARD_PRESSED : KEYBOARD_RELEASED;
}

int TDeckProBoard::transmitRadio(const uint8_t* data, size_t len)
{
    const uint32_t timeout_ms = radioTxTimeoutMs(radio_, len);
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_tx",
                       200U))
    {
        return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.startTransmit(data, len);
    sharedSpiUnlock();
    if (rc != RADIOLIB_ERR_NONE)
    {
        return rc;
    }

    const uint32_t started_ms = millis();
    while (digitalRead(profile().lora.irq) == LOW)
    {
        if (static_cast<uint32_t>(millis() - started_ms) > timeout_ms)
        {
            if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                               "tdeck_pro_radio_tx_timeout",
                               200U))
            {
                return RADIOLIB_ERR_TX_TIMEOUT;
            }
            sharedSpiPrepareDevice(profile().lora.cs);
            (void)radio_.finishTransmit();
            sharedSpiUnlock();
            return RADIOLIB_ERR_TX_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_tx_finish",
                       200U))
    {
        return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int finish_rc = radio_.finishTransmit();
    sharedSpiUnlock();
    return finish_rc;
}

int TDeckProBoard::startRadioReceive()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_rx_start",
                       200U))
    {
        return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.startReceive();
    sharedSpiUnlock();
    return rc;
}

bool TDeckProBoard::quiesceForExternalStorage()
{
    if (!isRadioOnline())
    {
        return true;
    }

    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_external_storage_standby",
                       200U))
    {
        return false;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.standby();
    sharedSpiUnlock();
    return rc == RADIOLIB_ERR_NONE;
}

uint32_t TDeckProBoard::getRadioIrqFlags()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_irq",
                       200U))
    {
        return 0;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const uint32_t flags = radio_.getIrqFlags();
    sharedSpiUnlock();
    return flags;
}

int TDeckProBoard::getRadioPacketLength(bool update)
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_length",
                       200U))
    {
        return -1;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int len = static_cast<int>(radio_.getPacketLength(update));
    sharedSpiUnlock();
    return len;
}

int TDeckProBoard::readRadioData(uint8_t* buf, size_t len)
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_read",
                       200U))
    {
        return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const int rc = radio_.readData(buf, len);
    sharedSpiUnlock();
    return rc;
}

void TDeckProBoard::clearRadioIrqFlags(uint32_t flags)
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_clear_irq",
                       200U))
    {
        return;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    radio_.clearIrqFlags(flags);
    sharedSpiUnlock();
}

float TDeckProBoard::getRadioRSSI()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_rssi",
                       200U))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const float rssi = radio_.getRSSI();
    sharedSpiUnlock();
    return rssi;
}

float TDeckProBoard::getRadioInstantRSSI()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_instant_rssi",
                       200U))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const float rssi = radio_.getRSSI(false);
    sharedSpiUnlock();
    return rssi;
}

float TDeckProBoard::getRadioSNR()
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_snr",
                       200U))
    {
        return std::numeric_limits<float>::quiet_NaN();
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    const float snr = radio_.getSNR();
    sharedSpiUnlock();
    return snr;
}

int TDeckProBoard::configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                      int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                      uint8_t crc_len)
{
    if (!sharedSpiLock(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                       "tdeck_pro_radio_config",
                       200U))
    {
        return RADIOLIB_ERR_SPI_CMD_TIMEOUT;
    }
    sharedSpiPrepareDevice(profile().lora.cs);
    int first_error = RADIOLIB_ERR_NONE;
    const auto note_error = [&first_error](int rc)
    {
        if (rc != RADIOLIB_ERR_NONE && first_error == RADIOLIB_ERR_NONE)
        {
            first_error = rc;
        }
    };
    note_error(radio_.setFrequency(freq_mhz));
    note_error(radio_.setBandwidth(bw_khz));
    note_error(radio_.setSpreadingFactor(sf));
    note_error(radio_.setCodingRate(cr_denom));
    applyTxPower(radio_, tx_power);
    note_error(radio_.setPreambleLength(preamble_len));
    note_error(radio_.setSyncWord(sync_word));
    note_error(radio_.setCRC(crc_len));
    sharedSpiUnlock();
    return first_error;
}

bool TDeckProBoard::initGPS()
{
    Serial2.end();
    Serial2.begin(profile().gps.uart.baud, SERIAL_8N1, profile().gps.uart.rx, profile().gps.uart.tx);
    delay(100);
    gps_.attach(&Serial2);
    gps_ready_ = true;
    Serial.printf("[%s] gps uart ready baud=%lu rx=%d tx=%d\n",
                  kTag,
                  static_cast<unsigned long>(profile().gps.uart.baud),
                  profile().gps.uart.rx,
                  profile().gps.uart.tx);
    return gps_ready_;
}

void TDeckProBoard::deinitGPS()
{
    Serial2.end();
    gps_ready_ = false;
}

void TDeckProBoard::powerControl(PowerCtrlChannel_t ch, bool enable)
{
    switch (ch)
    {
    case POWER_GPS:
        if (profile().gps.enable >= 0)
        {
            digitalWrite(profile().gps.enable, enable ? HIGH : LOW);
        }
        break;
    case POWER_SENSOR:
        if (profile().motion.enable_1v8 >= 0)
        {
            digitalWrite(profile().motion.enable_1v8, enable ? HIGH : LOW);
        }
        break;
    case POWER_RADIO:
        if (profile().lora.enable >= 0)
        {
            digitalWrite(profile().lora.enable, enable ? HIGH : LOW);
        }
        break;
    default:
        break;
    }
}

bool TDeckProBoard::syncTimeFromGPS(uint32_t gps_task_interval_ms)
{
    (void)gps_task_interval_ms;
    if (!gps_.date.isValid() || !gps_.time.isValid())
    {
        return false;
    }

    const int year = gps_.date.year();
    const uint8_t month = gps_.date.month();
    const uint8_t day = gps_.date.day();
    const uint8_t hour = gps_.time.hour();
    const uint8_t minute = gps_.time.minute();
    const uint8_t second = gps_.time.second();
    if (!gpsDatetimeValid(year, month, day, hour, minute, second))
    {
        return false;
    }

    const time_t epoch = gpsDatetimeToEpochUtc(year, month, day, hour, minute, second);
    if (epoch < kMinValidEpochSeconds)
    {
        return false;
    }

    timeval tv{};
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0)
    {
        return false;
    }
    rtc_ready_ = true;
    return true;
}

namespace
{
TDeckProBoard& getInstanceRef()
{
    return *TDeckProBoard::getInstance();
}
} // namespace

TDeckProBoard& instance = getInstanceRef();
BoardBase& board = getInstanceRef();

} // namespace boards::tdeck_pro

BoardBase& board = ::boards::tdeck_pro::instance;

#endif // defined(ARDUINO_T_DECK_PRO)
