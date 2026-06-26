#include "bsp/trail_mate_t_display_p4_runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "boards/t_display_p4/runtime_support.h"
#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_intr_alloc.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hi8561_driver.h"
#include "lvgl.h"
#include "rm69a10_driver.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/runtime/ui_feedback.h"

extern "C" void trail_mate_idf_note_user_activity(void);

namespace
{

constexpr const char* kTag = "t-display-p4-ui";
constexpr int kDsiPhyLdoChannel = 3;
constexpr int kDsiPhyLdoMv = 1830;
constexpr uint32_t kLvglTimerPeriodMs = 1;
constexpr int kLvglTaskStackSize = 12288;
constexpr int kStartupBrightnessPercent = 10;
constexpr int kBacklightPwmHz = 2000;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_0;
// Conservative I2C timeouts for touch — avoids random read failures
// when the bus is contended by expander / RTC / GPS/LoRa prepare.
// These will be tightened once the system I2C lock ordering is stable.
constexpr uint32_t kTouchI2cLockTimeoutMs = 50;
constexpr uint32_t kTouchI2cTransactionTimeoutMs = 30;

constexpr uint32_t kHi8561MemoryAddressEram = 0x20011000;
constexpr uint8_t kHi8561MaxDsramNum = 25;
constexpr uint32_t kHi8561DsramSectionInfoStartAddress = kHi8561MemoryAddressEram + 4;
constexpr uint32_t kHi8561EsramNumStartAddress =
    kHi8561DsramSectionInfoStartAddress + kHi8561MaxDsramNum * 8;
constexpr uint32_t kHi8561EsramSectionInfoStartAddress = kHi8561EsramNumStartAddress + 4;
constexpr uint16_t kHi8561MemoryEramSize = 4 * 1024;
constexpr uint8_t kHi8561TouchPointAddressOffset = 3;
constexpr uint8_t kHi8561SingleTouchPointDataSize = 5;
constexpr uint8_t kHi8561MaxTouchFingerCount = 10;

constexpr uint32_t kGt9895TouchInfoStartAddress = 0x00010308;
constexpr uint8_t kGt9895TouchPointAddressOffset = 8;
constexpr uint8_t kGt9895SingleTouchPointDataSize = 8;
constexpr uint8_t kGt9895MaxTouchFingerCount = 10;

constexpr uint32_t kKeyboardI2cDelayUs = 5;
constexpr uint32_t kKeyboardResetDelayMs = 10;
constexpr uint32_t kKeyboardPowerSettleDelayMs = 50;
constexpr uint32_t kKeyboardMonitorIntervalMs = 2000;
constexpr uint8_t kKeyboardAttachDebounceCount = 2;
constexpr uint8_t kKeyboardDetachDebounceCount = 3;
constexpr uint8_t kKeyboardI2cScanFirstAddress = 0x03;
constexpr uint8_t kKeyboardI2cScanLastAddress = 0x77;
constexpr uint8_t kXl9555RegOutputPort0 = 0x02;
constexpr uint8_t kXl9555RegConfigPort0 = 0x06;
constexpr uint8_t kXl9555Tca8418ResetMask = 1U << 6;
constexpr uint8_t kTca8418RegCfg = 0x01;
constexpr uint8_t kTca8418RegIntStat = 0x02;
constexpr uint8_t kTca8418RegKeyLockEventCount = 0x03;
constexpr uint8_t kTca8418RegKeyEventA = 0x04;
constexpr uint8_t kTca8418RegGpiEm1 = 0x20;
constexpr uint8_t kTca8418RegGpiEm2 = 0x21;
constexpr uint8_t kTca8418RegGpiEm3 = 0x22;
constexpr uint8_t kTca8418RegGpioIntEn1 = 0x1A;
constexpr uint8_t kTca8418RegGpioIntEn2 = 0x1B;
constexpr uint8_t kTca8418RegGpioIntEn3 = 0x1C;
constexpr uint8_t kTca8418RegKpGpio1 = 0x1D;
constexpr uint8_t kTca8418RegKpGpio2 = 0x1E;
constexpr uint8_t kTca8418RegKpGpio3 = 0x1F;
constexpr uint8_t kTca8418CfgAutoIncrementAndOverflowQueue = 0xA0;
constexpr uint8_t kTca8418IntKeyEvents = 0x01;
constexpr uint8_t kTca8418IntAll = 0x1F;
constexpr uint8_t kTca8418MaxKeyEvents = 10;
constexpr uint32_t kTca8418KeyCaps = 0x8B;
constexpr uint32_t kTca8418KeyAlt = 0x8C;
constexpr uint32_t kTca8418KeyCtrl = 0x8D;
constexpr uint32_t kTca8418KeyFn = 0x8E;
constexpr uint32_t kTca8418KeyWin = 0x8F;
constexpr uint32_t kTca8418KeyShift = 0x90;

constexpr std::array<uint32_t, 68> kTca8418LvglKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    LV_KEY_ESC, LV_KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    kTca8418KeyCaps, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    kTca8418KeyAlt, 'z', 'x', 'c', 'v', 'b', 'n', 'm',
    kTca8418KeyCtrl, LV_KEY_UP,
    kTca8418KeyFn, kTca8418KeyWin, kTca8418KeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', kTca8418KeyFn, LV_KEY_LEFT, LV_KEY_DOWN,
    0x91, '9', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0x92, LV_KEY_ENTER, '0', LV_KEY_RIGHT};

constexpr std::array<uint32_t, 68> kTca8418LvglShiftKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    0x8B, 0x8C, '!', '@', '#', '$', '%', '^', '&', '*',
    '\'', '_', '-', '+', '=', '\\', '|', ';', ':', '"',
    0x8D, '~', '[', ']', '{', '}', '\'', '`', '/', '?',
    0x8E, 0x8F, 0x90, 0x91, 0x92, '.', '<', '>',
    0x93, LV_KEY_UP,
    0x95, 0x96, kTca8418KeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', 0x9C, LV_KEY_LEFT, LV_KEY_DOWN,
    0x9F, '(', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0xA2, LV_KEY_ENTER, ')', LV_KEY_RIGHT};

esp_ldo_channel_handle_t s_dsi_phy_ldo = nullptr;
esp_lcd_dsi_bus_handle_t s_dsi_bus = nullptr;
esp_lcd_panel_io_handle_t s_panel_io = nullptr;
esp_lcd_panel_handle_t s_panel = nullptr;
lv_display_t* s_display = nullptr;
lv_indev_t* s_touch_indev = nullptr;
lv_indev_t* s_keyboard_indev = nullptr;
lv_timer_t* s_keyboard_monitor_timer = nullptr;
i2c_master_dev_handle_t s_touch_i2c_handle = nullptr;
bool s_lvgl_ready = false;
bool s_ready = false;
bool s_backlight_ready = false;
bool s_keyboard_ready = false;
bool s_keyboard_pressed = false;
bool s_keyboard_caps_lock = false;
bool s_keyboard_shift_active = false;
bool s_keyboard_interrupt_registered = false;
int s_brightness_percent = 0;
uint32_t s_keyboard_last_key = 0;
uint8_t s_keyboard_attach_probe_count = 0;
uint8_t s_keyboard_detach_probe_count = 0;
uint32_t s_hi8561_touch_info_start_address = 0;
float s_touch_scale_x = 1.0f;
float s_touch_scale_y = 1.0f;
volatile bool s_keyboard_irq_pending = false;

namespace runtime_support = boards::t_display_p4::runtime_support;

const boards::t_display_p4::BoardProfile::PanelGeometry& active_panel()
{
    return runtime_support::active_panel();
}

bool use_hi8561_panel()
{
    return runtime_support::configured_panel_type() == boards::t_display_p4::DisplayPanelType::Hi8561;
}

const char* panel_variant_label()
{
    return use_hi8561_panel() ? "T-Display-P4 TFT" : "T-Display-P4 AMOLED";
}

lcd_color_rgb_pixel_format_t panel_pixel_format()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return LCD_COLOR_PIXEL_FORMAT_RGB888;
#else
    return LCD_COLOR_PIXEL_FORMAT_RGB565;
#endif
}

lv_color_format_t lvgl_color_format()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return LV_COLOR_FORMAT_RGB888;
#else
    return LV_COLOR_FORMAT_RGB565;
#endif
}

int bits_per_pixel()
{
#if defined(CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB888)
    return 24;
#else
    return 16;
#endif
}

bool ensure_dsi_phy_power()
{
    if (s_dsi_phy_ldo != nullptr)
    {
        return true;
    }

    esp_ldo_channel_config_t ldo_cfg{};
    ldo_cfg.chan_id = kDsiPhyLdoChannel;
    ldo_cfg.voltage_mv = kDsiPhyLdoMv;
    if (esp_ldo_acquire_channel(&ldo_cfg, &s_dsi_phy_ldo) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to acquire DSI PHY LDO channel");
        return false;
    }
    return true;
}

bool init_backlight_backend()
{
    if (s_backlight_ready)
    {
        return true;
    }

    if (!use_hi8561_panel())
    {
        s_backlight_ready = true;
        return true;
    }

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_cfg.duty_resolution = LEDC_TIMER_12_BIT;
    timer_cfg.timer_num = kBacklightTimer;
    timer_cfg.freq_hz = kBacklightPwmHz;
    timer_cfg.clk_cfg = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure backlight LEDC timer");
        return false;
    }

    ledc_channel_config_t channel_cfg = {};
    channel_cfg.gpio_num = runtime_support::profile().lcd_backlight;
    channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_cfg.channel = kBacklightChannel;
    channel_cfg.intr_type = LEDC_INTR_DISABLE;
    channel_cfg.timer_sel = kBacklightTimer;
    channel_cfg.duty = 0;
    channel_cfg.hpoint = 0;
    if (ledc_channel_config(&channel_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure backlight LEDC channel");
        return false;
    }

    s_backlight_ready = true;
    return true;
}

bool ensure_touch_device()
{
    if (s_touch_i2c_handle != nullptr)
    {
        return true;
    }

    const runtime_support::SystemI2cDeviceConfig config{
        "touch",
        static_cast<uint16_t>(runtime_support::touch_i2c_address()),
        400000,
    };
    s_touch_i2c_handle = runtime_support::get_managed_system_i2c_device(config, 1000);
    if (s_touch_i2c_handle == nullptr)
    {
        ESP_LOGE(kTag,
                 "Failed to acquire touch device addr=0x%02X",
                 runtime_support::touch_i2c_address());
        return false;
    }

    const auto& panel = active_panel();
    s_touch_scale_x = (panel.touch_max_x > 0)
                          ? static_cast<float>(panel.width) / static_cast<float>(panel.touch_max_x)
                          : 1.0f;
    s_touch_scale_y = (panel.touch_max_y > 0)
                          ? static_cast<float>(panel.height) / static_cast<float>(panel.touch_max_y)
                          : 1.0f;
    ESP_LOGI(kTag,
             "Touch bus ready panel=%s addr=0x%02X scale=(%.3f,%.3f)",
             use_hi8561_panel() ? "hi8561" : "rm69a10",
             static_cast<unsigned>(runtime_support::touch_i2c_address()),
             static_cast<double>(s_touch_scale_x),
             static_cast<double>(s_touch_scale_y));
    return true;
}

bool init_hi8561_touch_address_info()
{
    if (s_hi8561_touch_info_start_address != 0)
    {
        return true;
    }
    if (!ensure_touch_device() || !runtime_support::lock_system_i2c(1000))
    {
        return false;
    }

    const uint8_t request[] = {
        0xF3,
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 24),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 16),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress >> 8),
        static_cast<uint8_t>(kHi8561EsramSectionInfoStartAddress),
        0x03,
    };
    uint8_t response[48] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle, request, sizeof(request), response, sizeof(response), 1000);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag, "HI8561 ESRAM probe failed: %s", esp_err_to_name(err));
        return false;
    }

    const uint32_t address = static_cast<uint32_t>(response[8]) |
                             (static_cast<uint32_t>(response[9]) << 8) |
                             (static_cast<uint32_t>(response[10]) << 16) |
                             (static_cast<uint32_t>(response[11]) << 24);
    if (address < kHi8561MemoryAddressEram ||
        address >= (kHi8561MemoryAddressEram + kHi8561MemoryEramSize))
    {
        ESP_LOGW(kTag, "HI8561 touch info address invalid: 0x%08lX", static_cast<unsigned long>(address));
        return false;
    }

    s_hi8561_touch_info_start_address = address;
    return true;
}

bool read_gt9895_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed);

bool reset_touch_controller()
{
    s_hi8561_touch_info_start_address = 0;
    if (!runtime_support::reset_touch_controller(10, 10, 10))
    {
        ESP_LOGE(kTag, "Touch reset failed");
        return false;
    }
    return true;
}

bool probe_touch_controller()
{
    if (!ensure_touch_device())
    {
        return false;
    }

    if (use_hi8561_panel())
    {
        if (!init_hi8561_touch_address_info())
        {
            ESP_LOGE(kTag, "HI8561 touch probe failed");
            return false;
        }
        ESP_LOGI(kTag,
                 "HI8561 touch probe ok info=0x%08lX",
                 static_cast<unsigned long>(s_hi8561_touch_info_start_address));
        return true;
    }

    int32_t x = 0;
    int32_t y = 0;
    bool pressed = false;
    if (!read_gt9895_touch(&x, &y, &pressed))
    {
        ESP_LOGE(kTag, "GT9895 touch probe failed");
        return false;
    }
    ESP_LOGI(kTag, "GT9895 touch probe ok");
    return true;
}

bool read_hi8561_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed)
{
    if (out_x == nullptr || out_y == nullptr || out_pressed == nullptr)
    {
        return false;
    }
    *out_pressed = false;

    if (!init_hi8561_touch_address_info() ||
        !runtime_support::lock_system_i2c(kTouchI2cLockTimeoutMs))
    {
        return false;
    }

    const uint32_t point_address =
        s_hi8561_touch_info_start_address + kHi8561TouchPointAddressOffset;
    const uint8_t request[] = {
        0xF3,
        static_cast<uint8_t>(point_address >> 24),
        static_cast<uint8_t>(point_address >> 16),
        static_cast<uint8_t>(point_address >> 8),
        static_cast<uint8_t>(point_address),
        0x03,
    };
    uint8_t response[kHi8561SingleTouchPointDataSize] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle,
        request,
        sizeof(request),
        response,
        sizeof(response),
        kTouchI2cTransactionTimeoutMs);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        return false;
    }

    const uint16_t raw_x = static_cast<uint16_t>((static_cast<uint16_t>(response[0]) << 8) |
                                                 response[1]);
    const uint16_t raw_y = static_cast<uint16_t>((static_cast<uint16_t>(response[2]) << 8) |
                                                 response[3]);
    if (raw_x == 0xFFFF && raw_y == 0xFFFF)
    {
        return true;
    }

    *out_x = std::clamp<int32_t>(raw_x, 0, active_panel().width - 1);
    *out_y = std::clamp<int32_t>(raw_y, 0, active_panel().height - 1);
    *out_pressed = true;
    return true;
}

bool read_gt9895_touch(int32_t* out_x, int32_t* out_y, bool* out_pressed)
{
    if (out_x == nullptr || out_y == nullptr || out_pressed == nullptr)
    {
        return false;
    }
    *out_pressed = false;

    if (!ensure_touch_device() || !runtime_support::lock_system_i2c(kTouchI2cLockTimeoutMs))
    {
        return false;
    }

    const uint8_t request[] = {
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 24),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 16),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress >> 8),
        static_cast<uint8_t>(kGt9895TouchInfoStartAddress),
    };
    constexpr size_t kReadSize =
        kGt9895TouchPointAddressOffset + kGt9895MaxTouchFingerCount * kGt9895SingleTouchPointDataSize;
    uint8_t response[kReadSize] = {};
    const esp_err_t err = i2c_master_transmit_receive(
        s_touch_i2c_handle,
        request,
        sizeof(request),
        response,
        sizeof(response),
        kTouchI2cTransactionTimeoutMs);
    runtime_support::unlock_system_i2c();
    if (err != ESP_OK)
    {
        return false;
    }

    const bool edge_touch = response[0] == 0x84;
    const uint8_t finger_count = response[2];
    const uint8_t effective_touch_count =
        (finger_count == 0 && edge_touch) ? 1 : finger_count;
    if (effective_touch_count == 0 || effective_touch_count > kGt9895MaxTouchFingerCount)
    {
        return true;
    }

    const uint8_t offset = kGt9895TouchPointAddressOffset;
    const uint16_t raw_x = static_cast<uint16_t>(response[offset + 2] |
                                                 (static_cast<uint16_t>(response[offset + 3]) << 8));
    const uint16_t raw_y = static_cast<uint16_t>(response[offset + 4] |
                                                 (static_cast<uint16_t>(response[offset + 5]) << 8));
    const int32_t scaled_x = static_cast<int32_t>(raw_x * s_touch_scale_x);
    const int32_t scaled_y = static_cast<int32_t>(raw_y * s_touch_scale_y);
    *out_x = std::clamp<int32_t>(scaled_x, 0, active_panel().width - 1);
    *out_y = std::clamp<int32_t>(scaled_y, 0, active_panel().height - 1);
    *out_pressed = true;
    return true;
}

bool s_touch_was_pressed = false;
std::uint32_t s_last_activity_note_ms = 0;

void note_touch_activity(bool pressed)
{
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
    const bool first_touch = pressed && !s_touch_was_pressed;
    const bool throttle_ok = pressed && (now - s_last_activity_note_ms) >= 500U;

    s_touch_was_pressed = pressed;

    if (pressed && (first_touch || throttle_ok))
    {
        s_last_activity_note_ms = now;
        trail_mate_idf_note_user_activity();
    }
}

const boards::t_display_p4::BoardProfile::KeyboardModule& keyboard_module()
{
    return boards::t_display_p4::TDisplayP4Board::keyboardModule();
}

struct KeyboardProbeResult
{
    bool supported = false;
    bool power_ready = false;
    bool gpio_ready = false;
    bool xl9555_ack = false;
    bool tca8418_ack = false;
    bool tca8418_cfg_readable = false;
    bool bq25896_ack = false;

    bool module_present() const
    {
        return xl9555_ack || tca8418_ack || bq25896_ack;
    }

    bool keyboard_controller_ready() const
    {
        return tca8418_ack && tca8418_cfg_readable;
    }
};

void keyboard_i2c_delay()
{
    esp_rom_delay_us(kKeyboardI2cDelayUs);
}

void keyboard_sda(bool high)
{
    gpio_set_level(static_cast<gpio_num_t>(keyboard_module().sda), high ? 1 : 0);
}

void keyboard_scl(bool high)
{
    gpio_set_level(static_cast<gpio_num_t>(keyboard_module().scl), high ? 1 : 0);
}

bool keyboard_read_sda()
{
    return gpio_get_level(static_cast<gpio_num_t>(keyboard_module().sda)) != 0;
}

bool keyboard_read_scl()
{
    return gpio_get_level(static_cast<gpio_num_t>(keyboard_module().scl)) != 0;
}

bool configure_keyboard_i2c_pins()
{
    const auto& kb = keyboard_module();
    if (kb.sda < 0 || kb.scl < 0 || kb.tca8418 == 0)
    {
        return false;
    }

    gpio_config_t sda_cfg{};
    sda_cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(kb.sda);
    sda_cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    sda_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    sda_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sda_cfg.intr_type = GPIO_INTR_DISABLE;
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
    sda_cfg.hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE;
#endif
    if (gpio_config(&sda_cfg) != ESP_OK)
    {
        ESP_LOGW(kTag, "Keyboard software I2C SDA GPIO config failed");
        return false;
    }

    gpio_config_t scl_cfg{};
    scl_cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(kb.scl);
    scl_cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    scl_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    scl_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    scl_cfg.intr_type = GPIO_INTR_DISABLE;
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
    scl_cfg.hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE;
#endif
    if (gpio_config(&scl_cfg) != ESP_OK)
    {
        ESP_LOGW(kTag, "Keyboard software I2C SCL GPIO config failed");
        return false;
    }

    keyboard_sda(true);
    keyboard_scl(true);
    keyboard_i2c_delay();
    return true;
}

void keyboard_i2c_start()
{
    keyboard_scl(true);
    keyboard_sda(true);
    keyboard_i2c_delay();
    keyboard_sda(false);
    keyboard_i2c_delay();
    keyboard_scl(false);
    keyboard_i2c_delay();
}

void keyboard_i2c_stop()
{
    keyboard_sda(false);
    keyboard_i2c_delay();
    keyboard_scl(true);
    keyboard_i2c_delay();
    keyboard_sda(true);
    keyboard_i2c_delay();
}

void keyboard_recover_i2c_bus(const char* reason)
{
    keyboard_sda(true);
    keyboard_scl(true);
    keyboard_i2c_delay();

    const bool idle_sda_before = keyboard_read_sda();
    const bool idle_scl_before = keyboard_read_scl();
    int pulses = 0;
    if (!idle_sda_before)
    {
        for (; pulses < 9 && !keyboard_read_sda(); ++pulses)
        {
            keyboard_scl(false);
            keyboard_i2c_delay();
            keyboard_scl(true);
            keyboard_i2c_delay();
        }
    }

    keyboard_i2c_stop();
    ESP_LOGI(kTag,
             "T-Display-P4 keyboard I2C bus ready reason=%s sda=%d scl=%d idle_before=%d/%d idle_after=%d/%d recover_pulses=%d",
             reason ? reason : "unknown",
             keyboard_module().sda,
             keyboard_module().scl,
             idle_sda_before,
             idle_scl_before,
             keyboard_read_sda(),
             keyboard_read_scl(),
             pulses);
}

bool keyboard_i2c_write_byte_raw(uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit)
    {
        keyboard_sda((value & (1U << bit)) != 0);
        keyboard_i2c_delay();
        keyboard_scl(true);
        keyboard_i2c_delay();
        keyboard_scl(false);
    }

    keyboard_sda(true);
    return true;
}

bool keyboard_i2c_wait_ack()
{
    keyboard_i2c_delay();
    keyboard_scl(true);
    keyboard_i2c_delay();
    const bool ack = !keyboard_read_sda();
    keyboard_scl(false);
    keyboard_i2c_delay();
    return ack;
}

bool keyboard_i2c_write_byte(uint8_t value)
{
    return keyboard_i2c_write_byte_raw(value) && keyboard_i2c_wait_ack();
}

uint8_t keyboard_i2c_read_byte(bool ack)
{
    uint8_t value = 0;
    keyboard_sda(true);
    for (int bit = 7; bit >= 0; --bit)
    {
        keyboard_scl(true);
        keyboard_i2c_delay();
        if (keyboard_read_sda())
        {
            value |= static_cast<uint8_t>(1U << bit);
        }
        keyboard_scl(false);
        keyboard_i2c_delay();
    }

    keyboard_sda(!ack);
    keyboard_scl(true);
    keyboard_i2c_delay();
    keyboard_scl(false);
    keyboard_sda(true);
    keyboard_i2c_delay();
    return value;
}

bool keyboard_probe_device_address(uint8_t device_address)
{
    keyboard_i2c_start();
    const bool ok = keyboard_i2c_write_byte(static_cast<uint8_t>(device_address << 1U));
    keyboard_i2c_stop();
    return ok;
}

bool keyboard_write_device_register(uint16_t device_address, uint8_t reg, uint8_t value)
{
    if (device_address == 0)
    {
        return false;
    }

    const uint8_t address = static_cast<uint8_t>(device_address << 1U);
    keyboard_i2c_start();
    const bool ok = keyboard_i2c_write_byte(address) &&
                    keyboard_i2c_write_byte(reg) &&
                    keyboard_i2c_write_byte(value);
    keyboard_i2c_stop();
    return ok;
}

bool keyboard_read_device_registers(uint16_t device_address, uint8_t reg, uint8_t* out, size_t len)
{
    if (device_address == 0 || !out || len == 0)
    {
        return false;
    }

    const uint8_t write_address = static_cast<uint8_t>(device_address << 1U);
    const uint8_t read_address = static_cast<uint8_t>(write_address | 0x01U);
    keyboard_i2c_start();
    if (!keyboard_i2c_write_byte(write_address) || !keyboard_i2c_write_byte(reg))
    {
        keyboard_i2c_stop();
        return false;
    }
    keyboard_i2c_start();
    if (!keyboard_i2c_write_byte(read_address))
    {
        keyboard_i2c_stop();
        return false;
    }

    for (size_t index = 0; index < len; ++index)
    {
        out[index] = keyboard_i2c_read_byte(index + 1 < len);
    }
    keyboard_i2c_stop();
    return true;
}

bool keyboard_read_device_register(uint16_t device_address, uint8_t reg, uint8_t* out)
{
    return keyboard_read_device_registers(device_address, reg, out, 1);
}

bool keyboard_write_register(uint8_t reg, uint8_t value)
{
    return keyboard_write_device_register(keyboard_module().tca8418, reg, value);
}

bool keyboard_read_registers(uint8_t reg, uint8_t* out, size_t len)
{
    return keyboard_read_device_registers(keyboard_module().tca8418, reg, out, len);
}

bool keyboard_read_register(uint8_t reg, uint8_t* out)
{
    return keyboard_read_registers(reg, out, 1);
}

bool keyboard_module_supported()
{
    return boards::t_display_p4::TDisplayP4Board::profile().supports_keyboard_module;
}

bool ensure_keyboard_module_power()
{
    if (!boards::t_display_p4::TDisplayP4Board::instance().ensureExternal3v3Power())
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kKeyboardPowerSettleDelayMs));
    return true;
}

KeyboardProbeResult probe_keyboard_module_bus()
{
    KeyboardProbeResult result{};
    result.supported = keyboard_module_supported();
    if (!result.supported)
    {
        return result;
    }

    result.power_ready = ensure_keyboard_module_power();
    if (!result.power_ready)
    {
        return result;
    }

    result.gpio_ready = configure_keyboard_i2c_pins();
    if (!result.gpio_ready)
    {
        return result;
    }
    keyboard_recover_i2c_bus("probe");

    const auto& kb = keyboard_module();
    result.xl9555_ack = kb.xl9555 != 0 && keyboard_probe_device_address(kb.xl9555);
    result.tca8418_ack = kb.tca8418 != 0 && keyboard_probe_device_address(kb.tca8418);
    result.bq25896_ack = kb.bq25896 != 0 && keyboard_probe_device_address(kb.bq25896);

    if (result.tca8418_ack)
    {
        uint8_t value = 0;
        result.tca8418_cfg_readable = keyboard_read_register(kTca8418RegCfg, &value);
    }
    return result;
}

void log_keyboard_probe_result(const char* reason,
                               const KeyboardProbeResult& probe,
                               bool warning)
{
    const auto& kb = keyboard_module();
    const char* tag_reason = reason ? reason : "unknown";
    if (warning)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard probe reason=%s supported=%d power=%d gpio=%d xl9555=0x%02X:%d tca8418=0x%02X:%d cfg=%d bq25896=0x%02X:%d module_present=%d controller_ready=%d",
                 tag_reason,
                 probe.supported,
                 probe.power_ready,
                 probe.gpio_ready,
                 static_cast<unsigned>(kb.xl9555),
                 probe.xl9555_ack,
                 static_cast<unsigned>(kb.tca8418),
                 probe.tca8418_ack,
                 probe.tca8418_cfg_readable,
                 static_cast<unsigned>(kb.bq25896),
                 probe.bq25896_ack,
                 probe.module_present(),
                 probe.keyboard_controller_ready());
        return;
    }

    ESP_LOGI(kTag,
             "T-Display-P4 keyboard probe reason=%s supported=%d power=%d gpio=%d xl9555=0x%02X:%d tca8418=0x%02X:%d cfg=%d bq25896=0x%02X:%d module_present=%d controller_ready=%d",
             tag_reason,
             probe.supported,
             probe.power_ready,
             probe.gpio_ready,
             static_cast<unsigned>(kb.xl9555),
             probe.xl9555_ack,
             static_cast<unsigned>(kb.tca8418),
             probe.tca8418_ack,
             probe.tca8418_cfg_readable,
             static_cast<unsigned>(kb.bq25896),
             probe.bq25896_ack,
             probe.module_present(),
             probe.keyboard_controller_ready());
}

bool probe_tca8418_presence()
{
    return probe_keyboard_module_bus().keyboard_controller_ready();
}

bool probe_keyboard_module_bus_presence()
{
    return probe_keyboard_module_bus().module_present();
}

void log_keyboard_i2c_scan(const char* reason)
{
    if (!keyboard_module_supported())
    {
        return;
    }
    if (!ensure_keyboard_module_power() ||
        !configure_keyboard_i2c_pins())
    {
        ESP_LOGW(kTag, "T-Display-P4 keyboard I2C scan skipped reason=%s power_or_gpio_failed",
                 reason ? reason : "unknown");
        return;
    }
    keyboard_recover_i2c_bus(reason ? reason : "scan");

    char found[192] = {};
    size_t used = 0;
    int count = 0;
    bool found_xl9555 = false;
    bool found_tca8418 = false;
    bool found_bq25896 = false;
    const auto& kb = keyboard_module();
    for (uint8_t address = kKeyboardI2cScanFirstAddress;
         address <= kKeyboardI2cScanLastAddress;
         ++address)
    {
        if (!keyboard_probe_device_address(address))
        {
            continue;
        }

        const int written = std::snprintf(found + used,
                                          sizeof(found) - used,
                                          "%s0x%02X",
                                          count == 0 ? "" : " ",
                                          static_cast<unsigned>(address));
        if (written > 0)
        {
            used += std::min<size_t>(static_cast<size_t>(written),
                                     used < sizeof(found) ? sizeof(found) - used - 1 : 0);
        }
        ++count;
        found_xl9555 = found_xl9555 || address == kb.xl9555;
        found_tca8418 = found_tca8418 || address == kb.tca8418;
        found_bq25896 = found_bq25896 || address == kb.bq25896;
    }

    if (count == 0)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard I2C scan reason=%s sda=%d scl=%d found=none expected_xl9555=0x%02X expected_tca8418=0x%02X expected_bq25896=0x%02X",
                 reason ? reason : "unknown",
                 kb.sda,
                 kb.scl,
                 static_cast<unsigned>(kb.xl9555),
                 static_cast<unsigned>(kb.tca8418),
                 static_cast<unsigned>(kb.bq25896));
        return;
    }

    ESP_LOGI(kTag,
             "T-Display-P4 keyboard I2C scan reason=%s sda=%d scl=%d found_count=%d found=%s expected_xl9555=0x%02X:%d expected_tca8418=0x%02X:%d expected_bq25896=0x%02X:%d",
             reason ? reason : "unknown",
             kb.sda,
             kb.scl,
             count,
             found,
             static_cast<unsigned>(kb.xl9555),
             found_xl9555,
             static_cast<unsigned>(kb.tca8418),
             found_tca8418,
             static_cast<unsigned>(kb.bq25896),
             found_bq25896);
}

void log_keyboard_hardware_i2c_scan(const char* reason)
{
    if (!keyboard_module_supported())
    {
        return;
    }
    const auto& kb = keyboard_module();
    if (kb.sda < 0 || kb.scl < 0)
    {
        return;
    }

    i2c_master_bus_config_t bus_cfg{};
    bus_cfg.i2c_port = -1;
    bus_cfg.sda_io_num = static_cast<gpio_num_t>(kb.sda);
    bus_cfg.scl_io_num = static_cast<gpio_num_t>(kb.scl);
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t bus_err = i2c_new_master_bus(&bus_cfg, &bus);
#if SOC_LP_I2C_SUPPORTED
    if (bus_err == ESP_ERR_NOT_FOUND)
    {
        bus_cfg.i2c_port = SOC_HP_I2C_NUM;
        bus_cfg.lp_source_clk = LP_I2C_SCLK_DEFAULT;
        bus_err = i2c_new_master_bus(&bus_cfg, &bus);
    }
#endif
    if (bus_err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard HW I2C scan unavailable reason=%s sda=%d scl=%d err=%s",
                 reason ? reason : "unknown",
                 kb.sda,
                 kb.scl,
                 esp_err_to_name(bus_err));
        return;
    }

    char found[192] = {};
    size_t used = 0;
    int count = 0;
    bool found_xl9555 = false;
    bool found_tca8418 = false;
    bool found_bq25896 = false;
    for (uint8_t address = kKeyboardI2cScanFirstAddress;
         address <= kKeyboardI2cScanLastAddress;
         ++address)
    {
        if (i2c_master_probe(bus, address, 20) != ESP_OK)
        {
            continue;
        }

        const int written = std::snprintf(found + used,
                                          sizeof(found) - used,
                                          "%s0x%02X",
                                          count == 0 ? "" : " ",
                                          static_cast<unsigned>(address));
        if (written > 0)
        {
            used += std::min<size_t>(static_cast<size_t>(written),
                                     used < sizeof(found) ? sizeof(found) - used - 1 : 0);
        }
        ++count;
        found_xl9555 = found_xl9555 || address == kb.xl9555;
        found_tca8418 = found_tca8418 || address == kb.tca8418;
        found_bq25896 = found_bq25896 || address == kb.bq25896;
    }

    (void)i2c_del_master_bus(bus);
    if (count == 0)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard HW I2C scan reason=%s sda=%d scl=%d found=none expected_xl9555=0x%02X expected_tca8418=0x%02X expected_bq25896=0x%02X",
                 reason ? reason : "unknown",
                 kb.sda,
                 kb.scl,
                 static_cast<unsigned>(kb.xl9555),
                 static_cast<unsigned>(kb.tca8418),
                 static_cast<unsigned>(kb.bq25896));
        return;
    }

    ESP_LOGI(kTag,
             "T-Display-P4 keyboard HW I2C scan reason=%s sda=%d scl=%d found_count=%d found=%s expected_xl9555=0x%02X:%d expected_tca8418=0x%02X:%d expected_bq25896=0x%02X:%d",
             reason ? reason : "unknown",
             kb.sda,
             kb.scl,
             count,
             found,
             static_cast<unsigned>(kb.xl9555),
             found_xl9555,
             static_cast<unsigned>(kb.tca8418),
             found_tca8418,
             static_cast<unsigned>(kb.bq25896),
             found_bq25896);
}

void IRAM_ATTR keyboard_irq_isr(void* arg)
{
    (void)arg;
    s_keyboard_irq_pending = true;
}

bool keyboard_irq_line_active()
{
    const auto& kb = keyboard_module();
    return kb.interrupt >= 0 && gpio_get_level(static_cast<gpio_num_t>(kb.interrupt)) == 0;
}

void remove_keyboard_interrupt()
{
    const auto& kb = keyboard_module();
    if (kb.interrupt < 0 || !s_keyboard_interrupt_registered)
    {
        s_keyboard_irq_pending = false;
        return;
    }

    const gpio_num_t irq_pin = static_cast<gpio_num_t>(kb.interrupt);
    gpio_intr_disable(irq_pin);
    gpio_isr_handler_remove(irq_pin);
    s_keyboard_interrupt_registered = false;
    s_keyboard_irq_pending = false;
}

bool configure_keyboard_interrupt_pin()
{
    const auto& kb = keyboard_module();
    if (kb.interrupt < 0)
    {
        ESP_LOGW(kTag, "T-Display-P4 keyboard interrupt pin is not configured");
        return false;
    }

    const gpio_num_t irq_pin = static_cast<gpio_num_t>(kb.interrupt);
    gpio_config_t cfg{};
    cfg.pin_bit_mask = 1ULL << static_cast<uint32_t>(kb.interrupt);
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&cfg) != ESP_OK)
    {
        ESP_LOGW(kTag, "T-Display-P4 keyboard interrupt GPIO config failed pin=%d", kb.interrupt);
        return false;
    }

    const esp_err_t service_err = gpio_install_isr_service(0);
    if (service_err != ESP_OK && service_err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard interrupt ISR service install failed err=%s",
                 esp_err_to_name(service_err));
        return false;
    }

    if (s_keyboard_interrupt_registered)
    {
        gpio_isr_handler_remove(irq_pin);
        s_keyboard_interrupt_registered = false;
    }

    const esp_err_t add_err = gpio_isr_handler_add(irq_pin, keyboard_irq_isr, nullptr);
    if (add_err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard interrupt handler add failed pin=%d err=%s",
                 kb.interrupt,
                 esp_err_to_name(add_err));
        return false;
    }

    gpio_intr_enable(irq_pin);
    s_keyboard_interrupt_registered = true;
    s_keyboard_irq_pending = keyboard_irq_line_active();
    ESP_LOGI(kTag,
             "T-Display-P4 keyboard interrupt armed pin=%d active_low=1 pending=%d",
             kb.interrupt,
             s_keyboard_irq_pending);
    return true;
}

bool set_keyboard_expander_reset_pin(bool high)
{
    const auto& kb = keyboard_module();
    uint8_t output = 0xFF;
    if (!keyboard_read_device_register(kb.xl9555, kXl9555RegOutputPort0, &output))
    {
        return false;
    }

    if (high)
    {
        output |= kXl9555Tca8418ResetMask;
    }
    else
    {
        output &= static_cast<uint8_t>(~kXl9555Tca8418ResetMask);
    }
    return keyboard_write_device_register(kb.xl9555, kXl9555RegOutputPort0, output);
}

bool reset_tca8418_via_keyboard_expander()
{
    const auto& kb = keyboard_module();
    if (kb.xl9555 == 0)
    {
        return false;
    }

    uint8_t config = 0xFF;
    if (!keyboard_read_device_register(kb.xl9555, kXl9555RegConfigPort0, &config))
    {
        return false;
    }
    config &= static_cast<uint8_t>(~kXl9555Tca8418ResetMask);
    if (!keyboard_write_device_register(kb.xl9555, kXl9555RegConfigPort0, config))
    {
        return false;
    }

    if (!set_keyboard_expander_reset_pin(true))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kKeyboardResetDelayMs));
    if (!set_keyboard_expander_reset_pin(false))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kKeyboardResetDelayMs));
    if (!set_keyboard_expander_reset_pin(true))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kKeyboardResetDelayMs));
    return true;
}

uint8_t mask_for_count(int count)
{
    if (count <= 0)
    {
        return 0;
    }
    if (count >= 8)
    {
        return 0xFF;
    }
    return static_cast<uint8_t>((1U << count) - 1U);
}

bool configure_tca8418_keypad()
{
    const auto& kb = keyboard_module();
    if (!ensure_keyboard_module_power() ||
        !configure_keyboard_i2c_pins())
    {
        return false;
    }
    keyboard_recover_i2c_bus("tca8418_init");

    const bool reset_ok = reset_tca8418_via_keyboard_expander();
    if (reset_ok)
    {
        ESP_LOGI(kTag, "T-Display-P4 keyboard module reset via XL9555 IO6");
    }
    else
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 keyboard module XL9555 reset unavailable; trying TCA8418 init");
    }

    uint8_t cfg = 0;
    if (!keyboard_read_register(kTca8418RegCfg, &cfg))
    {
        ESP_LOGW(kTag, "T-Display-P4 keyboard TCA8418 config register not readable");
        return false;
    }

    const uint8_t row_mask = mask_for_count(kb.rows);
    const uint8_t col_low_mask = mask_for_count(std::min(kb.columns, 8));
    const uint8_t col_high_mask = kb.columns > 8 ? mask_for_count(kb.columns - 8) : 0;

    if (!keyboard_write_register(kTca8418RegCfg, kTca8418CfgAutoIncrementAndOverflowQueue) ||
        !keyboard_write_register(kTca8418RegGpiEm1, 0xFF) ||
        !keyboard_write_register(kTca8418RegGpiEm2, 0xFF) ||
        !keyboard_write_register(kTca8418RegGpiEm3, 0xFF) ||
        !keyboard_write_register(kTca8418RegGpioIntEn1, 0xFF) ||
        !keyboard_write_register(kTca8418RegGpioIntEn2, 0xFF) ||
        !keyboard_write_register(kTca8418RegGpioIntEn3, 0xFF) ||
        !keyboard_write_register(kTca8418RegKpGpio1, row_mask) ||
        !keyboard_write_register(kTca8418RegKpGpio2, col_low_mask) ||
        !keyboard_write_register(kTca8418RegKpGpio3, col_high_mask) ||
        !keyboard_read_register(kTca8418RegCfg, &cfg) ||
        !keyboard_write_register(kTca8418RegCfg,
                                 static_cast<uint8_t>((cfg & 0xF0U) | kTca8418IntKeyEvents)) ||
        !keyboard_write_register(kTca8418RegIntStat, kTca8418IntAll))
    {
        ESP_LOGW(kTag, "T-Display-P4 keyboard TCA8418 keypad init sequence failed");
        return false;
    }

    if (!configure_keyboard_interrupt_pin())
    {
        return false;
    }

    s_keyboard_irq_pending = keyboard_irq_line_active();
    ESP_LOGI(kTag,
             "T-Display-P4 keyboard TCA8418 configured matrix=%dx%d irq_pin=%d pending=%d",
             kb.columns,
             kb.rows,
             kb.interrupt,
             s_keyboard_irq_pending);
    return true;
}

bool is_keyboard_modifier(uint32_t key)
{
    return key == kTca8418KeyFn ||
           key == kTca8418KeyWin ||
           key == kTca8418KeyAlt ||
           key == kTca8418KeyCtrl ||
           key == kTca8418KeyCaps ||
           key == kTca8418KeyShift;
}

uint32_t resolve_keyboard_key(uint8_t key_num, bool pressed)
{
    if (key_num == 0 || key_num > kTca8418LvglKeyMap.size())
    {
        return 0;
    }

    uint32_t key = s_keyboard_shift_active
                       ? kTca8418LvglShiftKeyMap[key_num - 1]
                       : kTca8418LvglKeyMap[key_num - 1];

    if (key == kTca8418KeyCaps)
    {
        if (pressed)
        {
            s_keyboard_caps_lock = !s_keyboard_caps_lock;
        }
        return 0;
    }

    if (key == kTca8418KeyShift)
    {
        s_keyboard_shift_active = pressed;
        return 0;
    }

    if (is_keyboard_modifier(key) || key >= 0x80)
    {
        return 0;
    }

    if (pressed && s_keyboard_caps_lock && key >= 'a' && key <= 'z')
    {
        key = key - 'a' + 'A';
    }
    return key;
}

bool poll_keyboard_event(uint32_t* out_key, bool* out_pressed)
{
    if (!out_key || !out_pressed)
    {
        return false;
    }
    if (!s_keyboard_irq_pending)
    {
        return false;
    }
    s_keyboard_irq_pending = false;

    uint8_t irq_status = 0;
    if (!keyboard_read_register(kTca8418RegIntStat, &irq_status))
    {
        s_keyboard_irq_pending = keyboard_irq_line_active();
        return false;
    }
    if ((irq_status & kTca8418IntKeyEvents) == 0)
    {
        if (irq_status != 0)
        {
            (void)keyboard_write_register(kTca8418RegIntStat, irq_status);
        }
        s_keyboard_irq_pending = keyboard_irq_line_active();
        return false;
    }

    uint8_t count_reg = 0;
    if (!keyboard_read_register(kTca8418RegKeyLockEventCount, &count_reg))
    {
        s_keyboard_irq_pending = keyboard_irq_line_active();
        return false;
    }

    const uint8_t event_count = std::min<uint8_t>(count_reg & 0x0F, kTca8418MaxKeyEvents);
    if (event_count == 0)
    {
        (void)keyboard_write_register(kTca8418RegIntStat, kTca8418IntKeyEvents);
        s_keyboard_irq_pending = keyboard_irq_line_active();
        return false;
    }

    uint8_t events[kTca8418MaxKeyEvents] = {};
    if (!keyboard_read_registers(kTca8418RegKeyEventA, events, event_count))
    {
        s_keyboard_irq_pending = keyboard_irq_line_active();
        return false;
    }

    bool found = false;
    for (uint8_t index = 0; index < event_count; ++index)
    {
        const bool pressed = (events[index] & 0x80U) != 0;
        const uint8_t key_num = events[index] & 0x7FU;
        if (key_num > 96)
        {
            continue;
        }

        const uint32_t key = resolve_keyboard_key(key_num, pressed);
        if (key == 0)
        {
            continue;
        }

        *out_key = key;
        *out_pressed = pressed;
        found = true;
    }

    (void)keyboard_write_register(kTca8418RegIntStat, kTca8418IntKeyEvents);
    s_keyboard_irq_pending = keyboard_irq_line_active();
    return found;
}

bool init_keyboard_backend(bool log_missing = true)
{
    if (s_keyboard_ready)
    {
        return true;
    }

    auto& board = boards::t_display_p4::TDisplayP4Board::instance();
    board.setKeyboardReady(false);

    if (!keyboard_module_supported())
    {
        return false;
    }

    const KeyboardProbeResult probe = probe_keyboard_module_bus();
    if (!probe.module_present())
    {
        if (log_missing)
        {
            ESP_LOGI(kTag,
                     "T-Display-P4 keyboard module not detected; touch IME remains enabled");
            log_keyboard_probe_result("startup_not_detected", probe, true);
            log_keyboard_i2c_scan("startup_not_detected");
            log_keyboard_hardware_i2c_scan("startup_not_detected");
        }
        return false;
    }

    if (!probe.keyboard_controller_ready())
    {
        if (log_missing)
        {
            ESP_LOGI(kTag,
                     "T-Display-P4 keyboard module present but TCA8418 is not usable; touch IME remains enabled");
            log_keyboard_probe_result("startup_controller_not_ready", probe, true);
            log_keyboard_i2c_scan("startup_controller_not_ready");
            log_keyboard_hardware_i2c_scan("startup_controller_not_ready");
        }
        return false;
    }

    if (!configure_tca8418_keypad())
    {
        if (log_missing)
        {
            ESP_LOGI(kTag,
                     "T-Display-P4 keyboard controller init failed; touch IME remains enabled");
            log_keyboard_probe_result("startup_init_failed", probe, true);
            log_keyboard_i2c_scan("startup_init_failed");
            log_keyboard_hardware_i2c_scan("startup_init_failed");
        }
        return false;
    }

    s_keyboard_ready = true;
    board.setKeyboardReady(true);
    ESP_LOGI(kTag,
             "T-Display-P4 keyboard module detected addr=0x%02X matrix=%dx%d",
             static_cast<unsigned>(keyboard_module().tca8418),
             keyboard_module().columns,
             keyboard_module().rows);
    log_keyboard_probe_result("startup_ready", probe, false);
    return true;
}

void keyboard_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    if (!s_keyboard_ready)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint32_t key = 0;
    bool pressed = false;
    if (poll_keyboard_event(&key, &pressed))
    {
        if (key <= 0x7FU && ui_get_active_app() == nullptr)
        {
            const char key_char = static_cast<char>(key);
            const int key_state = pressed ? 1 : 0;
            if (ui::menu_runtime::handleWalkieKey(key_char, key_state) ||
                ui::menu_runtime::handleShortcutKey(key_char, key_state))
            {
                if (pressed)
                {
                    trail_mate_idf_note_user_activity();
                }
                s_keyboard_pressed = false;
                data->state = LV_INDEV_STATE_RELEASED;
                data->key = 0;
                return;
            }
        }

        s_keyboard_pressed = pressed;
        if (key != 0)
        {
            s_keyboard_last_key = key;
        }
        if (pressed)
        {
            trail_mate_idf_note_user_activity();
        }
    }

    data->state = s_keyboard_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->key = s_keyboard_last_key;
}

void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    int32_t x = 0;
    int32_t y = 0;
    bool pressed = false;
    const bool ok = use_hi8561_panel() ? read_hi8561_touch(&x, &y, &pressed)
                                       : read_gt9895_touch(&x, &y, &pressed);

    if (!ok || !pressed)
    {
        note_touch_activity(false);
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;

    // Debounce user-activity notification: only fire on the first touch
    // press edge, then at most once every 500 ms while the finger stays
    // down.  Without this throttle, every LVGL input poll (~50-100 Hz)
    // would hit the screen_sleep runtime and risk re-entrant UI updates.
    note_touch_activity(true);
}

bool create_touch_indev()
{
    if (!reset_touch_controller() || !probe_touch_controller())
    {
        return false;
    }

    s_touch_indev = lv_indev_create();
    if (s_touch_indev == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create LVGL touch input device");
        return false;
    }

    lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
    lv_indev_set_display(s_touch_indev, s_display);
    ESP_LOGI(kTag, "LVGL touch input registered");
    return true;
}

void clear_keyboard_runtime_state();

bool register_keyboard_indev()
{
    if (s_keyboard_indev != nullptr)
    {
        return true;
    }

    s_keyboard_indev = lv_indev_create();
    if (s_keyboard_indev == nullptr)
    {
        ESP_LOGW(kTag, "Failed to create LVGL keyboard input device");
        clear_keyboard_runtime_state();
        return false;
    }

    lv_indev_set_type(s_keyboard_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_keyboard_indev, keyboard_read_cb);
    lv_indev_set_display(s_keyboard_indev, s_display);
    ESP_LOGI(kTag, "LVGL keyboard input registered");
    return true;
}

bool create_keyboard_indev()
{
    if (!init_keyboard_backend())
    {
        return true;
    }
    return register_keyboard_indev();
}

void clear_keyboard_runtime_state()
{
    remove_keyboard_interrupt();
    s_keyboard_ready = false;
    s_keyboard_pressed = false;
    s_keyboard_caps_lock = false;
    s_keyboard_shift_active = false;
    s_keyboard_last_key = 0;
    boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
}

void detach_keyboard_module(bool notify_user)
{
    if (s_keyboard_indev != nullptr)
    {
        lv_indev_delete(s_keyboard_indev);
        s_keyboard_indev = nullptr;
        ESP_LOGI(kTag, "LVGL keyboard input removed");
    }
    clear_keyboard_runtime_state();
    if (notify_user)
    {
        ::ui::feedback::show_notice("Keyboard module removed", 2200);
    }
}

bool attach_keyboard_module(bool notify_user)
{
    if (!init_keyboard_backend(false))
    {
        return false;
    }
    if (!register_keyboard_indev())
    {
        return false;
    }

    s_keyboard_attach_probe_count = kKeyboardAttachDebounceCount;
    s_keyboard_detach_probe_count = 0;
    if (notify_user)
    {
        ::ui::feedback::show_notice("Keyboard module connected", 2200);
    }
    return true;
}

void keyboard_module_monitor_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!keyboard_module_supported())
    {
        return;
    }

    const bool present = s_keyboard_ready ? probe_tca8418_presence()
                                          : probe_keyboard_module_bus_presence();
    if (present)
    {
        s_keyboard_attach_probe_count =
            std::min<uint8_t>(kKeyboardAttachDebounceCount,
                              static_cast<uint8_t>(s_keyboard_attach_probe_count + 1));
        s_keyboard_detach_probe_count = 0;
    }
    else
    {
        s_keyboard_detach_probe_count =
            std::min<uint8_t>(kKeyboardDetachDebounceCount,
                              static_cast<uint8_t>(s_keyboard_detach_probe_count + 1));
        s_keyboard_attach_probe_count = 0;
    }

    if (s_keyboard_ready)
    {
        if (s_keyboard_detach_probe_count >= kKeyboardDetachDebounceCount)
        {
            ESP_LOGI(kTag, "T-Display-P4 keyboard module removal detected");
            detach_keyboard_module(true);
        }
        return;
    }

    if (s_keyboard_attach_probe_count >= kKeyboardAttachDebounceCount)
    {
        if (attach_keyboard_module(true))
        {
            ESP_LOGI(kTag, "T-Display-P4 keyboard module hotplug detected");
        }
        else
        {
            s_keyboard_attach_probe_count = 0;
        }
    }
}

void start_keyboard_module_monitor()
{
    if (!keyboard_module_supported() || s_keyboard_monitor_timer != nullptr)
    {
        return;
    }

    s_keyboard_attach_probe_count = s_keyboard_ready ? kKeyboardAttachDebounceCount : 0;
    s_keyboard_detach_probe_count = 0;
    s_keyboard_monitor_timer =
        lv_timer_create(keyboard_module_monitor_cb, kKeyboardMonitorIntervalMs, nullptr);
    if (s_keyboard_monitor_timer == nullptr)
    {
        ESP_LOGW(kTag, "Failed to create keyboard module monitor timer");
        return;
    }
    ESP_LOGI(kTag,
             "T-Display-P4 keyboard module monitor enabled interval=%lums attach=%u detach=%u",
             static_cast<unsigned long>(kKeyboardMonitorIntervalMs),
             static_cast<unsigned>(kKeyboardAttachDebounceCount),
             static_cast<unsigned>(kKeyboardDetachDebounceCount));
}

bool create_panel()
{
    if (s_panel != nullptr)
    {
        return true;
    }
    if (!ensure_dsi_phy_power())
    {
        return false;
    }

    const auto& panel = active_panel();

    esp_lcd_dsi_bus_config_t bus_cfg{};
    bus_cfg.bus_id = 0;
    bus_cfg.num_data_lanes = static_cast<uint8_t>(panel.lane_num);
    bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = static_cast<uint32_t>(panel.lane_bit_rate_mbps);
    if (esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to create DSI bus");
        return false;
    }

    esp_lcd_dbi_io_config_t dbi_cfg{};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits = 8;
    dbi_cfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_panel_io) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to create DSI DBI IO");
        return false;
    }

    esp_lcd_dpi_panel_config_t dpi_cfg{};
    dpi_cfg.virtual_channel = 0;
    dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = static_cast<uint32_t>(panel.dpi_clock_mhz);
    dpi_cfg.pixel_format = panel_pixel_format();
    dpi_cfg.num_fbs = 1;
    dpi_cfg.video_timing.h_size = static_cast<uint32_t>(panel.width);
    dpi_cfg.video_timing.v_size = static_cast<uint32_t>(panel.height);
    dpi_cfg.video_timing.hsync_pulse_width = static_cast<uint32_t>(panel.hsync);
    dpi_cfg.video_timing.hsync_back_porch = static_cast<uint32_t>(panel.hbp);
    dpi_cfg.video_timing.hsync_front_porch = static_cast<uint32_t>(panel.hfp);
    dpi_cfg.video_timing.vsync_pulse_width = static_cast<uint32_t>(panel.vsync);
    dpi_cfg.video_timing.vsync_back_porch = static_cast<uint32_t>(panel.vbp);
    dpi_cfg.video_timing.vsync_front_porch = static_cast<uint32_t>(panel.vfp);
    dpi_cfg.flags.use_dma2d = true;

    esp_err_t err = ESP_FAIL;
    if (use_hi8561_panel())
    {
        hi8561_vendor_config_t vendor_cfg{};
        vendor_cfg.init_cmds = nullptr;
        vendor_cfg.init_cmds_size = 0;
        vendor_cfg.mipi_config.dsi_bus = s_dsi_bus;
        vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
        vendor_cfg.mipi_config.lane_num = static_cast<uint8_t>(panel.lane_num);

        esp_lcd_panel_dev_config_t panel_cfg{};
        panel_cfg.reset_gpio_num = -1;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = bits_per_pixel();
        panel_cfg.vendor_config = &vendor_cfg;
        err = esp_lcd_new_panel_hi8561(s_panel_io, &panel_cfg, &s_panel);
    }
    else
    {
        rm69a10_vendor_config_t vendor_cfg{};
        vendor_cfg.init_cmds = nullptr;
        vendor_cfg.init_cmds_size = 0;
        vendor_cfg.mipi_config.dsi_bus = s_dsi_bus;
        vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
        vendor_cfg.mipi_config.lane_num = static_cast<uint8_t>(panel.lane_num);

        esp_lcd_panel_dev_config_t panel_cfg{};
        panel_cfg.reset_gpio_num = -1;
        panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_cfg.bits_per_pixel = bits_per_pixel();
        panel_cfg.vendor_config = &vendor_cfg;
        err = esp_lcd_new_panel_rm69a10(s_panel_io, &panel_cfg, &s_panel);
    }
    if (err != ESP_OK || s_panel == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create LCD panel");
        return false;
    }

    if (esp_lcd_panel_reset(s_panel) != ESP_OK ||
        esp_lcd_panel_init(s_panel) != ESP_OK ||
        esp_lcd_panel_disp_on_off(s_panel, true) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to initialize LCD panel");
        return false;
    }

    return true;
}

bool init_lvgl()
{
    if (s_lvgl_ready)
    {
        return true;
    }

    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.timer_period_ms = kLvglTimerPeriodMs;
    lvgl_cfg.task_stack = kLvglTaskStackSize;
    if (lvgl_port_init(&lvgl_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to initialize LVGL port");
        return false;
    }

    s_lvgl_ready = true;
    return true;
}

bool create_display()
{
    if (s_display != nullptr)
    {
        return true;
    }
    if (!create_panel() || !init_lvgl())
    {
        return false;
    }

    const auto& panel = active_panel();
    lvgl_port_display_cfg_t disp_cfg{};
    disp_cfg.io_handle = s_panel_io;
    disp_cfg.panel_handle = s_panel;
    disp_cfg.control_handle = nullptr;
    disp_cfg.buffer_size =
        static_cast<uint32_t>(panel.width * CONFIG_TRAIL_MATE_T_DISPLAY_P4_LVGL_BUFFER_LINES);
    disp_cfg.double_buffer = true;
    disp_cfg.trans_size = 0;
    disp_cfg.hres = static_cast<uint32_t>(panel.width);
    disp_cfg.vres = static_cast<uint32_t>(panel.height);
    disp_cfg.monochrome = false;
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.color_format = lvgl_color_format();
#endif
#if CONFIG_TRAIL_MATE_T_DISPLAY_P4_PIXEL_FORMAT_RGB565
    disp_cfg.flags.buff_dma = true;
#else
    disp_cfg.flags.buff_dma = false;
#endif
    disp_cfg.flags.buff_spiram = true;
    disp_cfg.flags.sw_rotate = true;
#if LVGL_VERSION_MAJOR >= 9
    disp_cfg.flags.swap_bytes = false;
#endif
    disp_cfg.flags.full_refresh = 0;
    disp_cfg.flags.direct_mode = 0;

    lvgl_port_display_dsi_cfg_t dsi_cfg{};
    dsi_cfg.flags.avoid_tearing = false;

    s_display = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (s_display == nullptr)
    {
        ESP_LOGE(kTag, "Failed to add DSI display to LVGL");
        return false;
    }

    lv_display_set_rotation(s_display, LV_DISPLAY_ROTATION_90);
    return true;
}

void create_boot_screen()
{
    if (s_display == nullptr || !trail_mate_t_display_p4_display_lock(1000))
    {
        return;
    }

    lv_obj_t* screen = lv_screen_active();
    if (screen != nullptr)
    {
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x07131F), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* title = lv_label_create(screen);
        lv_label_set_text(title, "TrailMate");
        lv_obj_set_style_text_color(title, lv_color_hex(0xE7F4FF), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -16);

        lv_obj_t* subtitle = lv_label_create(screen);
        lv_label_set_text(subtitle, panel_variant_label());
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x6DB8E8), 0);
        lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 14);
    }

    trail_mate_t_display_p4_display_unlock();
}

esp_err_t set_brightness_percent(int brightness_percent)
{
    const int clamped = std::clamp(brightness_percent, 0, 100);
    s_brightness_percent = clamped;

    if (use_hi8561_panel())
    {
        if (!init_backlight_backend())
        {
            return ESP_FAIL;
        }

        const uint32_t duty = static_cast<uint32_t>((4095U * static_cast<uint32_t>(clamped)) / 100U);
        if (ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty) != ESP_OK ||
            ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel) != ESP_OK)
        {
            return ESP_FAIL;
        }
        return ESP_OK;
    }

    if (s_panel == nullptr)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t raw = static_cast<uint8_t>((255U * static_cast<uint32_t>(clamped)) / 100U);
    return set_rm69a10_brightness(s_panel, raw);
}

} // namespace

extern "C" bool trail_mate_t_display_p4_display_runtime_init(void)
{
    if (s_ready)
    {
        return true;
    }

    ESP_LOGI(kTag,
             "Initializing %s display runtime panel=%s size=%dx%d",
             panel_variant_label(),
             use_hi8561_panel() ? "hi8561" : "rm69a10",
             active_panel().width,
             active_panel().height);

    if (!create_display())
    {
        return false;
    }
    if (!init_backlight_backend())
    {
        return false;
    }
    if (!create_touch_indev())
    {
        return false;
    }
    (void)create_keyboard_indev();
    start_keyboard_module_monitor();
    if (set_brightness_percent(kStartupBrightnessPercent) != ESP_OK)
    {
        ESP_LOGW(kTag, "Initial brightness update failed");
    }

    create_boot_screen();

    s_ready = true;
    ESP_LOGI(kTag,
             "Display runtime ready buffer_lines=%d brightness=%d",
             CONFIG_TRAIL_MATE_T_DISPLAY_P4_LVGL_BUFFER_LINES,
             s_brightness_percent);
    return true;
}

extern "C" bool trail_mate_t_display_p4_display_runtime_is_ready(void)
{
    return s_ready;
}

extern "C" bool trail_mate_t_display_p4_display_lock(uint32_t timeout_ms)
{
    return s_lvgl_ready && lvgl_port_lock(timeout_ms);
}

extern "C" void trail_mate_t_display_p4_display_unlock(void)
{
    if (s_lvgl_ready)
    {
        lvgl_port_unlock();
    }
}

extern "C" esp_err_t trail_mate_t_display_p4_display_set_brightness_percent(int brightness_percent)
{
    return set_brightness_percent(brightness_percent);
}
