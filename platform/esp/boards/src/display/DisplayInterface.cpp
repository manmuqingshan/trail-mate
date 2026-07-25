#include "display/DisplayInterface.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "sys/bus_access_scope.h"
#include "sys/clock.h"
#include <Arduino.h>
#include <vector>

#define DISP_CMD_MADCTL (0x36)
#define DISP_CMD_CASET (0x2A)
#define DISP_CMD_RASET (0x2B)
#define DISP_CMD_RAMWR (0x2C)
#define DISP_CMD_SLPIN (0x10)
#define DISP_CMD_SLPOUT (0x11)

namespace
{
constexpr uint32_t kDisplayFrameWaitMs = 45U;
constexpr uint32_t kDisplayControlWaitMs = 50U;
constexpr uint32_t kDisplayLockTimeoutLogIntervalMs = 1000;
constexpr uint32_t kDisplayBusResource = platform::esp::common::SharedSpiCoordinator::kSharedBusResource;
constexpr uint32_t kDisplayCommandId = 0x44495350U; // "DISP"
constexpr const char* kDisplayOwner = "display";

uint32_t s_last_display_lock_timeout_log_ms = 0;
uint32_t s_suppressed_display_lock_timeout_logs = 0;

sys::runtime::BusAcquireRequest make_display_request(
    sys::runtime::BusAccessPolicy policy,
    uint32_t wait_ms)
{
    sys::runtime::BusAcquireRequest request{};
    request.resource = kDisplayBusResource;
    request.policy = policy;
    request.command_id = kDisplayCommandId;
    request.origin = kDisplayCommandId;
    request.deadline_ms = sys::millis_now() + wait_ms;
    request.owner_label = kDisplayOwner;
    return request;
}

void log_display_lock_timeout(const char* op,
                              const sys::runtime::BusAcquireResult& result)
{
    auto& coordinator = platform::esp::common::shared_spi_coordinator();
    const uint32_t now_ms = millis();
    ++s_suppressed_display_lock_timeout_logs;
    if (s_last_display_lock_timeout_log_ms != 0 &&
        now_ms - s_last_display_lock_timeout_log_ms < kDisplayLockTimeoutLogIntervalMs)
    {
        return;
    }

    Serial.printf("[SPI][DISPLAY] acquire_timeout op=%s status=%u wait_ms=%lu "
                  "owner=%s task=%s held_ms=%lu max_hold_ms=%lu "
                  "release_mismatches=%lu suppressed=%lu\n",
                  op ? op : "",
                  static_cast<unsigned>(result.status),
                  static_cast<unsigned long>(result.diagnostics.wait_ms),
                  coordinator.ownerLabel(),
                  coordinator.ownerTaskName(),
                  static_cast<unsigned long>(coordinator.ownerHeldMs(now_ms)),
                  static_cast<unsigned long>(coordinator.maximumHoldMs()),
                  static_cast<unsigned long>(coordinator.releaseMismatches()),
                  static_cast<unsigned long>(s_suppressed_display_lock_timeout_logs - 1));
    s_suppressed_display_lock_timeout_logs = 0;
    s_last_display_lock_timeout_log_ms = now_ms;
}
} // namespace

void LilyGoDispArduinoSPI::setBrightness(uint8_t level)
{
    _brightness = level;
}

bool LilyGoDispArduinoSPI::init(int sck,
                                int miso,
                                int mosi,
                                int cs,
                                int rst,
                                int dc,
                                int backlight,
                                uint32_t freq_Mhz,
                                SPIClass& spi)
{
    _spi = &spi;
    _spi_freq = freq_Mhz * 1000U * 1000U;

    if (rst != -1)
    {
        pinMode(rst, OUTPUT);
        digitalWrite(rst, LOW);
        delay(20);
        digitalWrite(rst, HIGH);
        delay(120);
    }
    _width = _init_width;
    _height = _init_height;

    _cs = cs;
    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);

    _dc = dc;
    pinMode(_dc, OUTPUT);
    digitalWrite(_dc, HIGH);

    _backlight = backlight;
    if (_backlight != -1)
    {
        pinMode(_backlight, OUTPUT);
        // Keep the panel dark until init commands and the first clear frame finish,
        // otherwise some ST77xx boards briefly show uninitialized garbage.
        digitalWrite(_backlight, LOW);
    }

    _spi->begin(sck, miso, mosi);

    for (uint32_t i = 0; i < _init_list_length; i++)
    {
        writeParams(_init_list[i].cmd, (uint8_t*)_init_list[i].data, _init_list[i].len & 0x1F);
        if (_init_list[i].len & 0x80)
        {
            delay(120);
        }
    }

    setRotation(0);

    std::vector<uint16_t> draw_buf(_width * _height, 0x0000);
    pushColors(0, 0, _width, _height, draw_buf.data());
    if (_backlight != -1)
    {
        digitalWrite(_backlight, (_brightness > 0) ? HIGH : LOW);
    }
    return true;
}

void LilyGoDispArduinoSPI::end()
{
    // Shared bus, no deinit
}

uint8_t LilyGoDispArduinoSPI::getRotation()
{
    return _rotation;
}

void LilyGoDispArduinoSPI::setRotation(uint8_t rotation)
{
    const uint8_t next_rotation = rotation % 4;
    const DispRotationConfig_t& config = _rotation_configs[next_rotation];
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("setRotation", bus.result());
        return;
    }
    writeCommandLocked(DISP_CMD_MADCTL);
    writeDataLocked(config.madCmd);
    _rotation = next_rotation;
    _width = config.width;
    _height = config.height;
    _offset_x = config.offset_x;
    _offset_y = config.offset_y;
    bus.release();
}

void LilyGoDispArduinoSPI::pushColors(uint16_t* data, uint32_t len)
{
    (void)pushColorsResult(data, len);
}

bool LilyGoDispArduinoSPI::pushColorsResult(uint16_t* data, uint32_t len)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                             kDisplayFrameWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("pushColors", bus.result());
        return false;
    }
    pushColorsLocked(data, len);
    bus.release();
    return true;
}

void LilyGoDispArduinoSPI::pushColorsLocked(uint16_t* data, uint32_t len)
{
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    if (_transfer_config.rgb565_msb_first)
    {
        // Keep RGB565 wire ordering in the SPI driver instead of copying every
        // flush through a tiny stack chunk. The Arduino core writes pixels in
        // 16-bit MSB order for ILI9341/ST77xx-style panels.
        _spi->writePixels(data, len * sizeof(uint16_t));
    }
    else
    {
        _spi->writeBytes(reinterpret_cast<const uint8_t*>(data), len * sizeof(uint16_t));
    }
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
}

void LilyGoDispArduinoSPI::pushColors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t* color)
{
    (void)pushColorsResult(x1, y1, x2, y2, color);
}

bool LilyGoDispArduinoSPI::pushColorsResult(uint16_t x1,
                                            uint16_t y1,
                                            uint16_t x2,
                                            uint16_t y2,
                                            uint16_t* color)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                             kDisplayFrameWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("pushColorsArea", bus.result());
        return false;
    }
    setAddrWindowLocked(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    pushColorsLocked(color, x2 * y2);
    bus.release();
    return true;
}

void LilyGoDispArduinoSPI::sleep()
{
    writeCommand(DISP_CMD_SLPIN);
}

void LilyGoDispArduinoSPI::wakeup()
{
    writeCommand(DISP_CMD_SLPOUT);
}

void LilyGoDispArduinoSPI::setAddrWindow(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("setAddrWindow", bus.result());
        return;
    }
    setAddrWindowLocked(xs, ys, xe, ye);
    bus.release();
}

void LilyGoDispArduinoSPI::setAddrWindowLocked(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    xs += _offset_x;
    ys += _offset_y;
    xe += _offset_x;
    ye += _offset_y;
    CommandTable_t t[3] = {
        {DISP_CMD_CASET, {uint8_t(xs >> 8), (uint8_t)xs, uint8_t(xe >> 8), uint8_t(xe)}, 0x04},
        {DISP_CMD_RASET, {uint8_t(ys >> 8), (uint8_t)ys, uint8_t(ye >> 8), uint8_t(ye)}, 0x04},
        {DISP_CMD_RAMWR, {0x00}, 0x00},
    };
    for (uint32_t i = 0; i < 3; i++)
    {
        writeParamsLocked(t[i].cmd, t[i].data, t[i].len);
    }
}

void LilyGoDispArduinoSPI::writeCommand(uint8_t cmd)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("writeCommand", bus.result());
        return;
    }
    writeCommandLocked(cmd);
    bus.release();
}

void LilyGoDispArduinoSPI::writeCommandLocked(uint8_t cmd)
{
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, LOW);
    _spi->write(cmd);
    digitalWrite(_dc, HIGH);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
}

void LilyGoDispArduinoSPI::writeData(uint8_t data)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("writeData", bus.result());
        return;
    }
    writeDataLocked(data);
    bus.release();
}

void LilyGoDispArduinoSPI::writeDataLocked(uint8_t data)
{
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    _spi->write(data);
    _spi->endTransaction();
    digitalWrite(_cs, HIGH);
}

void LilyGoDispArduinoSPI::writeParams(uint8_t cmd, uint8_t* data, size_t length)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("writeParams", bus.result());
        return;
    }
    writeParamsLocked(cmd, data, length);
    bus.release();
}

void LilyGoDispArduinoSPI::writeParamsLocked(uint8_t cmd, uint8_t* data, size_t length)
{
    writeCommandLocked(cmd);
    if (data != nullptr)
    {
        for (size_t i = 0; i < length; i++)
        {
            writeDataLocked(data[i]);
        }
    }
}
