#pragma once

#include <cstdint>

namespace boards::t_echo_lite
{

constexpr int pinNum(int port, int pin)
{
    return port * 32 + pin;
}

struct BoardProfile
{
    struct LedPins
    {
        int red = -1;
        int green = -1;
        int blue = -1;
        int status = -1;
        int notification = -1;
        bool active_high = false;
        bool notification_shares_status = false;
    };

    struct InputPins
    {
        int button_primary = -1;
        int button_secondary = -1;
        int joystick_up = -1;
        int joystick_down = -1;
        int joystick_left = -1;
        int joystick_right = -1;
        int joystick_press = -1;
        bool buttons_need_pullup = true;
        bool joystick_need_pullup = true;
        bool joystick_is_two_way = false;
        uint16_t debounce_ms = 35;
    };

    struct I2cPins
    {
        int sda = -1;
        int scl = -1;
        uint8_t address = 0x00;
    };

    struct UartPins
    {
        int rx = -1;
        int tx = -1;
        int aux = -1;
    };

    struct SpiPins
    {
        int sck = -1;
        int miso = -1;
        int mosi = -1;
        int cs = -1;
    };

    struct EpaperPins
    {
        SpiPins spi{};
        int busy = -1;
        int reset = -1;
        int dc = -1;
        int bs1 = -1;
        int sram_cs = -1;
        int width = 176;
        int height = 192;
    };

    struct LoraPins
    {
        SpiPins spi{};
        int dio1 = -1;
        int dio2 = -1;
        int busy = -1;
        int reset = -1;
        int power_en = -1;
        int rf_vc1 = -1;
        int rf_vc2 = -1;
        bool dio2_controls_rf_switch = false;
        float dio3_tcxo_voltage = 1.8f;
    };

    struct GpsProfile
    {
        UartPins uart{};
        int pps = -1;
        uint32_t baud_rate = 9600;
    };

    struct BatteryProfile
    {
        int adc_pin = -1;
        int enable_pin = -1;
        uint8_t adc_resolution_bits = 12;
        float aref_voltage = 3.0f;
        float adc_multiplier = 2.0f;
    };

    struct BuzzerProfile
    {
        int pin = -1;
        bool active_high = true;
    };

    struct KeyboardProfile
    {
        uint8_t address = 0x34;
        int interrupt_pin = -1;
        uint8_t rows = 5;
        uint8_t columns = 4;
    };

    struct KeyboardBacklightProfile
    {
        uint8_t address = 0x20;
        uint16_t max_brightness = 4095;
    };

    struct AudioProfile
    {
        uint8_t codec_address = 0x18;
        int adc_data = -1;
        int dac_data = -1;
        int bit_clock = -1;
        int master_clock = -1;
        int word_select = -1;
    };

    struct HapticProfile
    {
        uint8_t address = 0x58;
    };

    struct ProductBoundary
    {
        bool supports_meshtastic = true;
        bool supports_meshcore = true;
        bool supports_ble = false;
        bool supports_lora = true;
        bool supports_gnss = true;
        bool supports_team = false;
        bool supports_hostlink = false;
        bool supports_sdcard = false;
        bool supports_cjk_input = true;
        bool supports_pinyin_ime = true;
        bool supports_touch = false;
        bool supports_keyboard = true;
        bool supports_audio = true;
        bool supports_keyboard_backlight = true;
    };

    struct ProductIdentity
    {
        const char* long_name = "T-Echo Lite";
        const char* short_name = "T-Echo Lite";
        const char* ble_name = "T-Echo Lite";
    };

    LedPins leds{};
    InputPins inputs{};
    I2cPins i2c{};
    EpaperPins epaper{};
    UartPins jlink_cdc{};
    LoraPins lora{};
    GpsProfile gps{};
    BatteryProfile battery{};
    BuzzerProfile buzzer{};
    KeyboardProfile keyboard{};
    KeyboardBacklightProfile keyboard_backlight{};
    AudioProfile audio{};
    HapticProfile haptic{};
    int peripheral_3v3_enable = -1;
    bool has_screen = true;
    bool use_ssd1306 = false;
    uint32_t max_flash_size = 815104;
    uint32_t max_ram_size = 248832;
    uint32_t bootloader_settings_addr = 0xFF000;
    ProductIdentity identity{};
    ProductBoundary boundary{};
};

constexpr BoardProfile makeBoardProfile()
{
    BoardProfile p{};
    p.leds = {pinNum(1, 7), pinNum(1, 5), pinNum(1, 14), pinNum(1, 7), pinNum(1, 5), false, false};
    p.inputs = {-1, -1, -1, -1, -1, -1, -1, true, true, false, 35};
    p.i2c = {pinNum(1, 4), pinNum(1, 2), 0x00};
    p.epaper.spi = {pinNum(0, 19), -1, pinNum(0, 20), pinNum(0, 22)};
    p.epaper.busy = pinNum(0, 3);
    p.epaper.reset = pinNum(0, 28);
    p.epaper.dc = pinNum(0, 21);
    p.epaper.bs1 = pinNum(1, 12);
    p.epaper.sram_cs = -1;
    p.epaper.width = 176;
    p.epaper.height = 192;
    p.jlink_cdc = {pinNum(1, 10), pinNum(0, 29), -1};
    p.lora.spi = {pinNum(0, 13), pinNum(0, 17), pinNum(0, 15), pinNum(0, 11)};
    p.lora.dio1 = pinNum(1, 8);
    p.lora.dio2 = pinNum(0, 5);
    p.lora.busy = pinNum(0, 14);
    p.lora.reset = pinNum(0, 7);
    p.lora.rf_vc1 = pinNum(0, 27);
    p.lora.rf_vc2 = pinNum(1, 1);
    p.lora.dio2_controls_rf_switch = false;
    p.lora.dio3_tcxo_voltage = 1.8f;
    p.gps = {{pinNum(1, 10), pinNum(0, 29), pinNum(1, 11)}, pinNum(1, 15), 9600};
    p.battery = {pinNum(0, 2), pinNum(0, 31), 12, 3.0f, 2.0f};
    p.buzzer = {-1, true};
    p.keyboard = {0x34, pinNum(1, 3), 5, 4};
    p.keyboard_backlight = {0x20, 4095};
    p.audio = {0x18, pinNum(0, 23), pinNum(1, 6), pinNum(0, 10), pinNum(0, 9), pinNum(0, 25)};
    p.haptic = {0x58};
    p.peripheral_3v3_enable = pinNum(0, 30);
    p.identity = {"T-Echo Lite", "T-Echo Lite", "T-Echo Lite"};
    return p;
}

inline constexpr BoardProfile kBoardProfile = makeBoardProfile();

} // namespace boards::t_echo_lite
