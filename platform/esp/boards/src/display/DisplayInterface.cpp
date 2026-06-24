#include "display/DisplayInterface.h"
#include <Arduino.h>
#include <vector>

#define DISP_CMD_MADCTL (0x36)
#define DISP_CMD_CASET (0x2A)
#define DISP_CMD_RASET (0x2B)
#define DISP_CMD_RAMWR (0x2C)
#define DISP_CMD_SLPIN (0x10)
#define DISP_CMD_SLPOUT (0x11)

static LilyGoDispArduinoSPI* g_display_spi = nullptr;

namespace
{
// Display flush is the frame-critical client of the shared SPI bus. It uses a
// short bounded wait and drops the current flush if storage/radio work is
// holding the bus, so LVGL can return to input/timer processing instead of
// blocking the UI owner task indefinitely.
constexpr TickType_t kDisplayFrameLockWait = pdMS_TO_TICKS(35);
constexpr TickType_t kDisplayControlLockWait = pdMS_TO_TICKS(50);
constexpr uint32_t kDisplayLockTimeoutLogIntervalMs = 1000;

uint32_t s_last_display_lock_timeout_log_ms = 0;
uint32_t s_suppressed_display_lock_timeout_logs = 0;
volatile uint32_t s_last_display_spi_timeout_ms = 0;

bool lock_or_log(LilyGoDispArduinoSPI& spi, TickType_t wait_ticks, const char* op)
{
    if (spi.lock(wait_ticks, "display"))
    {
        return true;
    }

    const uint32_t now_ms = millis();
    ::platform::esp::common::note_display_spi_timeout(now_ms);
    ++s_suppressed_display_lock_timeout_logs;
    if (s_last_display_lock_timeout_log_ms == 0 ||
        now_ms - s_last_display_lock_timeout_log_ms >= kDisplayLockTimeoutLogIntervalMs)
    {
        Serial.printf("[SPI][DISPLAY] lock_timeout op=%s wait_ticks=%lu owner=%s task=%s native_holder=%s held_ms=%lu depth=%lu last_owner=%s last_task=%s last_held_ms=%lu last_release_age_ms=%lu suppressed=%lu\n",
                      op ? op : "",
                      static_cast<unsigned long>(wait_ticks),
                      spi.lockOwnerLabel(),
                      spi.lockOwnerTaskName(),
                      spi.nativeLockHolderTaskName(),
                      static_cast<unsigned long>(spi.lockHeldMs(now_ms)),
                      static_cast<unsigned long>(spi.lockDepth()),
                      spi.lastLockOwnerLabel(),
                      spi.lastLockOwnerTaskName(),
                      static_cast<unsigned long>(spi.lastLockHeldMs()),
                      static_cast<unsigned long>(spi.lastLockReleaseAgeMs(now_ms)),
                      static_cast<unsigned long>(s_suppressed_display_lock_timeout_logs - 1));
        s_suppressed_display_lock_timeout_logs = 0;
        s_last_display_lock_timeout_log_ms = now_ms;
    }
    return false;
}
} // namespace

namespace platform::esp::common
{

bool shared_spi_lock(TickType_t xTicksToWait)
{
    return shared_spi_lock_with_owner(xTicksToWait, "shared");
}

bool shared_spi_lock_with_owner(TickType_t xTicksToWait, const char* owner)
{
    return g_display_spi && g_display_spi->lock(xTicksToWait, owner);
}

void shared_spi_unlock()
{
    if (g_display_spi)
    {
        g_display_spi->unlock();
    }
}

void note_display_spi_timeout(uint32_t now_ms)
{
    s_last_display_spi_timeout_ms = now_ms;
}

uint32_t last_display_spi_timeout_ms()
{
    return s_last_display_spi_timeout_ms;
}

bool display_spi_recently_timed_out(uint32_t now_ms, uint32_t window_ms)
{
    const uint32_t last_timeout_ms = s_last_display_spi_timeout_ms;
    return last_timeout_ms != 0 &&
           static_cast<uint32_t>(now_ms - last_timeout_ms) < window_ms;
}

} // namespace platform::esp::common

bool LilyGoDispArduinoSPI::lock(TickType_t xTicksToWait, const char* owner)
{
    if (_lock == nullptr)
    {
        return false;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    TaskHandle_t native_holder = xSemaphoreGetMutexHolder(_lock);
    if (native_holder == current)
    {
        // Storage/runtime code can be called from a scope that already owns the
        // physical SPI bus; same-task reentry must not look like contention.
        ++_lock_depth;
        return true;
    }

    if (xSemaphoreTake(_lock, xTicksToWait) != pdTRUE)
    {
        return false;
    }

    // Keep owner diagnostics on the physical shared-bus mutex. The timeout log
    // is used by storage adapters to detect display pressure and back off.
    _lock_owner = current;
    _lock_depth = 1;
    _lock_owner_label = (owner && owner[0] != '\0') ? owner : "direct";
    _lock_owner_task_name = pcTaskGetName(current);
    _lock_acquired_ms = millis();
    return true;
}

void LilyGoDispArduinoSPI::unlock()
{
    if (_lock == nullptr)
    {
        return;
    }

    TaskHandle_t current = xTaskGetCurrentTaskHandle();
    TaskHandle_t native_holder = xSemaphoreGetMutexHolder(_lock);
    if (native_holder != current)
    {
        Serial.printf("[SPI][LOCK] unlock_skip task=%s owner=%s owner_task=%s native_holder=%s depth=%lu\n",
                      pcTaskGetName(current),
                      lockOwnerLabel(),
                      lockOwnerTaskName(),
                      nativeLockHolderTaskName(),
                      static_cast<unsigned long>(_lock_depth));
        return;
    }

    if (_lock_depth > 1)
    {
        --_lock_depth;
        return;
    }

    const uint32_t now_ms = millis();
    _last_lock_owner_label = _lock_owner_label;
    _last_lock_owner_task_name = _lock_owner_task_name;
    _last_lock_held_ms = _lock_acquired_ms == 0 ? 0 : static_cast<uint32_t>(now_ms - _lock_acquired_ms);
    _last_lock_released_ms = now_ms;

    if (xSemaphoreGive(_lock) != pdTRUE)
    {
        return;
    }

    _lock_depth = 0;
    _lock_owner = nullptr;
    _lock_owner_label = nullptr;
    _lock_owner_task_name = nullptr;
    _lock_acquired_ms = 0;
}

const char* LilyGoDispArduinoSPI::lockOwnerLabel() const
{
    return _lock_owner_label ? _lock_owner_label : "-";
}

const char* LilyGoDispArduinoSPI::lockOwnerTaskName() const
{
    return _lock_owner_task_name ? _lock_owner_task_name : "-";
}

const char* LilyGoDispArduinoSPI::nativeLockHolderTaskName() const
{
    TaskHandle_t holder = _lock ? xSemaphoreGetMutexHolder(_lock) : nullptr;
    return holder ? pcTaskGetName(holder) : "-";
}

uint32_t LilyGoDispArduinoSPI::lockHeldMs(uint32_t now_ms) const
{
    return _lock_acquired_ms == 0 ? 0 : static_cast<uint32_t>(now_ms - _lock_acquired_ms);
}

uint32_t LilyGoDispArduinoSPI::lockDepth() const
{
    return _lock_depth;
}

const char* LilyGoDispArduinoSPI::lastLockOwnerLabel() const
{
    return _last_lock_owner_label ? _last_lock_owner_label : "-";
}

const char* LilyGoDispArduinoSPI::lastLockOwnerTaskName() const
{
    return _last_lock_owner_task_name ? _last_lock_owner_task_name : "-";
}

uint32_t LilyGoDispArduinoSPI::lastLockHeldMs() const
{
    return _last_lock_held_ms;
}

uint32_t LilyGoDispArduinoSPI::lastLockReleaseAgeMs(uint32_t now_ms) const
{
    return _last_lock_released_ms == 0 ? 0 : static_cast<uint32_t>(now_ms - _last_lock_released_ms);
}

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
    _lock = xSemaphoreCreateMutex();
    if (_lock == nullptr)
    {
        return false;
    }

    _spi = &spi;
    g_display_spi = this;

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

    _spi_freq = freq_Mhz * 1000U * 1000U;

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
    _rotation = rotation % 4;
    uint8_t mad_cmd = _rotation_configs[_rotation].madCmd;
    writeCommand(DISP_CMD_MADCTL);
    writeData(mad_cmd);
    _width = _rotation_configs[_rotation].width;
    _height = _rotation_configs[_rotation].height;
    _offset_x = _rotation_configs[_rotation].offset_x;
    _offset_y = _rotation_configs[_rotation].offset_y;
}

void LilyGoDispArduinoSPI::pushColors(uint16_t* data, uint32_t len)
{
    if (!lock_or_log(*this, kDisplayFrameLockWait, "pushColors"))
    {
        return;
    }
    pushColorsLocked(data, len);
    unlock();
}

void LilyGoDispArduinoSPI::pushColorsLocked(uint16_t* data, uint32_t len)
{
    digitalWrite(_cs, LOW);
    _spi->beginTransaction(SPISettings(_spi_freq, MSBFIRST, SPI_MODE0));
    digitalWrite(_dc, HIGH);
    if (_transfer_config.rgb565_msb_first)
    {
        // Keep pixel byte-order handling in one place: display drivers describe
        // how RGB565 must be placed on the wire, and LVGL stays unaware of it.
        constexpr size_t kChunkPixels = 96;
        uint8_t chunk[kChunkPixels * sizeof(uint16_t)];
        uint32_t offset = 0;
        while (offset < len)
        {
            size_t batch = len - offset;
            if (batch > kChunkPixels)
            {
                batch = kChunkPixels;
            }
            for (size_t i = 0; i < batch; ++i)
            {
                const uint16_t pixel = data[offset + i];
                chunk[i * 2] = static_cast<uint8_t>(pixel >> 8);
                chunk[i * 2 + 1] = static_cast<uint8_t>(pixel & 0xFF);
            }
            _spi->writeBytes(chunk, batch * sizeof(uint16_t));
            offset += static_cast<uint32_t>(batch);
        }
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
    if (!lock_or_log(*this, kDisplayFrameLockWait, "pushColorsArea"))
    {
        return;
    }
    setAddrWindowLocked(x1, y1, x1 + x2 - 1, y1 + y2 - 1);
    pushColorsLocked(color, x2 * y2);
    unlock();
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
    if (!lock_or_log(*this, kDisplayControlLockWait, "setAddrWindow"))
    {
        return;
    }
    setAddrWindowLocked(xs, ys, xe, ye);
    unlock();
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
    if (!lock_or_log(*this, kDisplayControlLockWait, "writeCommand"))
    {
        return;
    }
    writeCommandLocked(cmd);
    unlock();
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
    if (!lock_or_log(*this, kDisplayControlLockWait, "writeData"))
    {
        return;
    }
    writeDataLocked(data);
    unlock();
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
    if (!lock_or_log(*this, kDisplayControlLockWait, "writeParams"))
    {
        return;
    }
    writeParamsLocked(cmd, data, length);
    unlock();
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
