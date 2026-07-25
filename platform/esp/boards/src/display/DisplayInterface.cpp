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
uint32_t s_display_frame_log_count = 0;

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
    Serial.printf("[DISPLAY][INIT] begin sck=%d miso=%d mosi=%d cs=%d rst=%d dc=%d "
                  "freq_hz=%lu init_cmds=%lu brightness=%u\n",
                  sck,
                  miso,
                  mosi,
                  cs,
                  rst,
                  dc,
                  static_cast<unsigned long>(_spi_freq),
                  static_cast<unsigned long>(_init_list_length),
                  static_cast<unsigned>(_brightness));

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
    Serial.printf("[DISPLAY][INIT] pins_ready cs=%d level=%d dc=%d level=%d spi_started=1\n",
                  _cs,
                  digitalRead(_cs),
                  _dc,
                  digitalRead(_dc));

    bool init_commands_ok = true;
    for (uint32_t i = 0; i < _init_list_length; i++)
    {
        const bool sent =
            writeParams(_init_list[i].cmd, (uint8_t*)_init_list[i].data, _init_list[i].len & 0x1F);
        init_commands_ok = init_commands_ok && sent;
        Serial.printf("[DISPLAY][INIT] cmd index=%lu cmd=0x%02X data_len=%u sent=%d\n",
                      static_cast<unsigned long>(i),
                      static_cast<unsigned>(_init_list[i].cmd),
                      static_cast<unsigned>(_init_list[i].len & 0x1F),
                      sent ? 1 : 0);
        if (_init_list[i].len & 0x80)
        {
            delay(120);
        }
    }
    if (!init_commands_ok)
    {
        Serial.printf("[DISPLAY][INIT] failed reason=init_command_transaction\n");
        return false;
    }

    setRotation(0);
    Serial.printf("[DISPLAY][INIT] rotation=0 logical=%ux%u offset=(%u,%u) "
                  "coord_requests=%lu busy=%lu failures=%lu\n",
                  static_cast<unsigned>(_width),
                  static_cast<unsigned>(_height),
                  static_cast<unsigned>(_offset_x),
                  static_cast<unsigned>(_offset_y),
                  static_cast<unsigned long>(
                      platform::esp::common::shared_spi_coordinator().displayFrameRequests()),
                  static_cast<unsigned long>(
                      platform::esp::common::shared_spi_coordinator().displayFrameBusyRetries()),
                  static_cast<unsigned long>(
                      platform::esp::common::shared_spi_coordinator().displayFrameFailures()));

    std::vector<uint16_t> draw_buf(_width * _height, 0x0000);
    const bool clear_sent = pushColorsResult(0, 0, _width, _height, draw_buf.data());
    Serial.printf("[DISPLAY][INIT] clear_frame sent=%d pixels=%lu completions=%lu\n",
                  clear_sent ? 1 : 0,
                  static_cast<unsigned long>(_width * _height),
                  static_cast<unsigned long>(
                      platform::esp::common::shared_spi_coordinator().displayFrameCompletions()));
    if (!clear_sent)
    {
        Serial.printf("[DISPLAY][INIT] failed reason=clear_frame_transaction\n");
        return false;
    }
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
    const uint32_t sequence = ++s_display_frame_log_count;
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                             kDisplayFrameWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("pushColors", bus.result());
        Serial.printf("[DISPLAY][TX] frame seq=%lu acquired=0 status=%u len=%lu\n",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned>(bus.status()),
                      static_cast<unsigned long>(len));
        return false;
    }
    if (sequence <= 8U)
    {
        Serial.printf("[DISPLAY][TX] frame seq=%lu acquired=1 wait_ms=%lu len=%lu bytes=%lu\n",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned long>(bus.diagnostics().wait_ms),
                      static_cast<unsigned long>(len),
                      static_cast<unsigned long>(len * sizeof(uint16_t)));
    }
    pushColorsLocked(data, len);
    bus.release();
    if (sequence <= 8U)
    {
        Serial.printf("[DISPLAY][TX] frame seq=%lu complete cs=%d dc=%d\n",
                      static_cast<unsigned long>(sequence),
                      digitalRead(_cs),
                      digitalRead(_dc));
    }
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
    const uint32_t sequence = ++s_display_frame_log_count;
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::DisplayFrameCritical,
                             kDisplayFrameWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("pushColorsArea", bus.result());
        Serial.printf("[DISPLAY][TX] area seq=%lu acquired=0 status=%u area=(%u,%u)+(%u,%u)\n",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned>(bus.status()),
                      static_cast<unsigned>(x1),
                      static_cast<unsigned>(y1),
                      static_cast<unsigned>(x2),
                      static_cast<unsigned>(y2));
        return false;
    }
    if (sequence <= 8U)
    {
        Serial.printf("[DISPLAY][TX] area seq=%lu acquired=1 wait_ms=%lu "
                      "area=(%u,%u)+(%u,%u) pixels=%lu\n",
                      static_cast<unsigned long>(sequence),
                      static_cast<unsigned long>(bus.diagnostics().wait_ms),
                      static_cast<unsigned>(x1),
                      static_cast<unsigned>(y1),
                      static_cast<unsigned>(x2),
                      static_cast<unsigned>(y2),
                      static_cast<unsigned long>(x2 * y2));
    }
    setAddrWindowLocked(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    pushColorsLocked(color, x2 * y2);
    bus.release();
    if (sequence <= 8U)
    {
        Serial.printf("[DISPLAY][TX] area seq=%lu complete cs=%d dc=%d\n",
                      static_cast<unsigned long>(sequence),
                      digitalRead(_cs),
                      digitalRead(_dc));
    }
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

bool LilyGoDispArduinoSPI::writeParams(uint8_t cmd, uint8_t* data, size_t length)
{
    sys::runtime::ScopedBusAccessToken bus(
        platform::esp::common::shared_spi_coordinator(),
        make_display_request(sys::runtime::BusAccessPolicy::InteractiveWorkerBounded,
                             kDisplayControlWaitMs));
    if (!bus.acquired())
    {
        log_display_lock_timeout("writeParams", bus.result());
        return false;
    }
    writeParamsLocked(cmd, data, length);
    bus.release();
    return true;
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
