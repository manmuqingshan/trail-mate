#include "bsp/trail_mate_t_display_p4_keyboard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"

extern "C" void trail_mate_idf_note_user_activity(void);

namespace
{

constexpr const char* kTag = "t-display-p4-kbd";

// P2 is a dedicated, bit-banged two-device bus on the K270 keyboard.  It is
// not the board SYS-I2C or external I2C bus, so it has one owner: this adapter.
constexpr uint32_t kI2cDelayUs = 5;
constexpr uint32_t kPowerSettleMs = 50;
constexpr uint32_t kResetDelayMs = 10;
constexpr uint32_t kPollIntervalMs = 30;
constexpr uint32_t kI2cFailureBackoffMs = 500;
constexpr uint32_t kI2cFailureLogIntervalMs = 2000;

constexpr uint8_t kXl9555RegOutputPort0 = 0x02;
constexpr uint8_t kXl9555RegConfigPort0 = 0x06;
constexpr uint8_t kXl9555Tca8418ResetMask = 1U << 6;

constexpr uint8_t kTca8418RegCfg = 0x01;
constexpr uint8_t kTca8418RegIntStat = 0x02;
constexpr uint8_t kTca8418RegKeyLockEventCount = 0x03;
constexpr uint8_t kTca8418RegKeyEventA = 0x04;
constexpr uint8_t kTca8418RegKpGpio1 = 0x1D;
constexpr uint8_t kTca8418RegKpGpio2 = 0x1E;
constexpr uint8_t kTca8418RegKpGpio3 = 0x1F;
constexpr uint8_t kTca8418CfgAutoIncrementAndOverflowQueue = 0xA0;
constexpr uint8_t kTca8418IntKeyEvents = 0x01;
constexpr uint8_t kTca8418IntAll = 0x1F;
constexpr uint8_t kTca8418MaxKeyEvents = 10;

constexpr uint32_t kKeyCaps = 0x8B;
constexpr uint32_t kKeyAlt = 0x8C;
constexpr uint32_t kKeyCtrl = 0x8D;
constexpr uint32_t kKeyFn = 0x8E;
constexpr uint32_t kKeyWin = 0x8F;
constexpr uint32_t kKeyShift = 0x90;
constexpr uint32_t kKeyF11 = 0x91;
constexpr uint32_t kKeyRecord = 0x92;
constexpr size_t kEventQueueCapacity = 16;

// These are the LilyGO TCA8418 scan maps used by the K270 keyboard example.
// The controller reports a key number (1-based), not a matrix coordinate.
constexpr std::array<uint32_t, 68> kKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    LV_KEY_ESC, LV_KEY_ESC, '1', '2', '3', '4', '5', '6', '7', '8',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
    kKeyCaps, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l',
    kKeyAlt, 'z', 'x', 'c', 'v', 'b', 'n', 'm',
    kKeyCtrl, LV_KEY_UP,
    kKeyFn, kKeyWin, kKeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', kKeyFn, LV_KEY_LEFT, LV_KEY_DOWN,
    0x91, '9', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0x92, LV_KEY_ENTER, '0', LV_KEY_RIGHT};

constexpr std::array<uint32_t, 68> kShiftKeyMap = {
    0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    kKeyCaps, kKeyAlt, '!', '@', '#', '$', '%', '^', '&', '*',
    '\'', '_', '-', '+', '=', '\\', '|', ';', ':', '"',
    kKeyCtrl, '~', '[', ']', '{', '}', ',', '`', '/', '?',
    kKeyFn, kKeyWin, kKeyShift, 0x91, 0x92, '.', '<', '>',
    0x93, LV_KEY_UP,
    0x95, 0x96, kKeyShift, LV_KEY_NEXT,
    ' ', ' ', ' ', 0x9C, LV_KEY_LEFT, LV_KEY_DOWN,
    0x9F, '(', LV_KEY_BACKSPACE, LV_KEY_ENTER, 0xA2, LV_KEY_ENTER, ')', LV_KEY_RIGHT};

bool s_ready = false;
bool s_caps_lock = false;
bool s_shift = false;
bool s_fn = false;
lv_timer_t* s_poll_timer = nullptr;
uint32_t s_next_i2c_attempt_ms = 0;
uint32_t s_last_i2c_failure_log_ms = 0;
std::array<uint32_t, kEventQueueCapacity> s_event_queue{};
size_t s_event_queue_head = 0;
size_t s_event_queue_count = 0;

const boards::t_display_p4::BoardProfile::KeyboardModule& keyboard_module()
{
    return boards::t_display_p4::TDisplayP4Board::profile().keyboard;
}

gpio_num_t sda_pin()
{
    return static_cast<gpio_num_t>(keyboard_module().sda);
}

gpio_num_t scl_pin()
{
    return static_cast<gpio_num_t>(keyboard_module().scl);
}

void i2c_delay()
{
    esp_rom_delay_us(kI2cDelayUs);
}

void set_sda(bool high)
{
    gpio_set_level(sda_pin(), high ? 1 : 0);
}

void set_scl(bool high)
{
    gpio_set_level(scl_pin(), high ? 1 : 0);
}

bool read_sda()
{
    return gpio_get_level(sda_pin()) != 0;
}

bool configure_bus_pins()
{
    const auto& keyboard = keyboard_module();
    if (keyboard.sda < 0 || keyboard.scl < 0)
    {
        return false;
    }

    gpio_config_t config{};
    config.pin_bit_mask = (1ULL << static_cast<uint32_t>(keyboard.sda)) |
                          (1ULL << static_cast<uint32_t>(keyboard.scl));
    config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK)
    {
        return false;
    }

    set_sda(true);
    set_scl(true);
    i2c_delay();
    return true;
}

void i2c_start()
{
    set_sda(true);
    set_scl(true);
    i2c_delay();
    set_sda(false);
    i2c_delay();
    set_scl(false);
    i2c_delay();
}

void i2c_stop()
{
    set_sda(false);
    i2c_delay();
    set_scl(true);
    i2c_delay();
    set_sda(true);
    i2c_delay();
}

bool write_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; --bit)
    {
        set_sda((value & (1U << bit)) != 0);
        i2c_delay();
        set_scl(true);
        i2c_delay();
        set_scl(false);
        i2c_delay();
    }

    set_sda(true);
    i2c_delay();
    set_scl(true);
    i2c_delay();
    const bool acknowledged = !read_sda();
    set_scl(false);
    i2c_delay();
    return acknowledged;
}

uint8_t read_byte(bool acknowledge)
{
    uint8_t value = 0;
    set_sda(true);
    for (int bit = 7; bit >= 0; --bit)
    {
        set_scl(true);
        i2c_delay();
        if (read_sda())
        {
            value |= static_cast<uint8_t>(1U << bit);
        }
        set_scl(false);
        i2c_delay();
    }

    set_sda(!acknowledge);
    i2c_delay();
    set_scl(true);
    i2c_delay();
    set_scl(false);
    set_sda(true);
    i2c_delay();
    return value;
}

bool probe(uint16_t address)
{
    i2c_start();
    const bool ok = write_byte(static_cast<uint8_t>(address << 1U));
    i2c_stop();
    return ok;
}

bool write_register(uint16_t address, uint8_t reg, uint8_t value)
{
    i2c_start();
    const bool ok = write_byte(static_cast<uint8_t>(address << 1U)) &&
                    write_byte(reg) &&
                    write_byte(value);
    i2c_stop();
    return ok;
}

bool read_registers(uint16_t address, uint8_t reg, uint8_t* out, size_t count)
{
    if (out == nullptr || count == 0)
    {
        return false;
    }

    i2c_start();
    const bool selected = write_byte(static_cast<uint8_t>(address << 1U)) && write_byte(reg);
    if (!selected)
    {
        i2c_stop();
        return false;
    }

    i2c_start();
    if (!write_byte(static_cast<uint8_t>((address << 1U) | 1U)))
    {
        i2c_stop();
        return false;
    }

    for (size_t index = 0; index < count; ++index)
    {
        out[index] = read_byte(index + 1U < count);
    }
    i2c_stop();
    return true;
}

bool read_register(uint16_t address, uint8_t reg, uint8_t* out)
{
    return read_registers(address, reg, out, 1);
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

bool reset_controller()
{
    const auto& keyboard = keyboard_module();
    uint8_t config = 0xFF;
    uint8_t output = 0xFF;
    if (!read_register(keyboard.xl9555, kXl9555RegConfigPort0, &config) ||
        !read_register(keyboard.xl9555, kXl9555RegOutputPort0, &output))
    {
        return false;
    }

    config &= static_cast<uint8_t>(~kXl9555Tca8418ResetMask);
    output |= kXl9555Tca8418ResetMask;
    if (!write_register(keyboard.xl9555, kXl9555RegConfigPort0, config) ||
        !write_register(keyboard.xl9555, kXl9555RegOutputPort0, output))
    {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));
    output &= static_cast<uint8_t>(~kXl9555Tca8418ResetMask);
    if (!write_register(keyboard.xl9555, kXl9555RegOutputPort0, output))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));

    output |= kXl9555Tca8418ResetMask;
    if (!write_register(keyboard.xl9555, kXl9555RegOutputPort0, output))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));
    return true;
}

bool initialize_controller()
{
    const auto& keyboard = keyboard_module();
    auto& board = boards::t_display_p4::TDisplayP4Board::instance();
    if (!board.ensureKeyboardLdo4Power())
    {
        ESP_LOGW(kTag, "P4 keyboard cannot enable P2 LDO4 at 3300mV");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kPowerSettleMs));

    if (!configure_bus_pins())
    {
        ESP_LOGW(kTag, "P4 keyboard cannot configure P2 bus pins");
        return false;
    }
    if (!probe(keyboard.xl9555))
    {
        ESP_LOGI(kTag, "P4 keyboard not attached: XL9555 0x%02X did not ACK",
                 static_cast<unsigned>(keyboard.xl9555));
        return false;
    }
    if (!reset_controller())
    {
        ESP_LOGW(kTag, "P4 keyboard XL9555 reset sequence failed");
        return false;
    }
    if (!probe(keyboard.tca8418))
    {
        ESP_LOGW(kTag, "P4 keyboard TCA8418 0x%02X did not ACK after reset",
                 static_cast<unsigned>(keyboard.tca8418));
        return false;
    }

    const uint8_t rows = mask_for_count(keyboard.rows);
    const uint8_t columns_low = mask_for_count(std::min(keyboard.columns, 8));
    const uint8_t columns_high =
        keyboard.columns > 8 ? mask_for_count(keyboard.columns - 8) : 0;
    const bool configured =
        write_register(keyboard.tca8418, kTca8418RegCfg,
                       kTca8418CfgAutoIncrementAndOverflowQueue) &&
        write_register(keyboard.tca8418, kTca8418RegKpGpio1, rows) &&
        write_register(keyboard.tca8418, kTca8418RegKpGpio2, columns_low) &&
        write_register(keyboard.tca8418, kTca8418RegKpGpio3, columns_high) &&
        write_register(keyboard.tca8418, kTca8418RegCfg,
                       static_cast<uint8_t>(kTca8418CfgAutoIncrementAndOverflowQueue |
                                            kTca8418IntKeyEvents)) &&
        write_register(keyboard.tca8418, kTca8418RegIntStat, kTca8418IntAll);
    if (!configured)
    {
        ESP_LOGW(kTag, "P4 keyboard TCA8418 scan window setup failed");
        return false;
    }

    s_caps_lock = false;
    s_shift = false;
    s_fn = false;
    s_event_queue_head = 0;
    s_event_queue_count = 0;
    s_next_i2c_attempt_ms = 0;
    s_last_i2c_failure_log_ms = 0;
    board.setKeyboardReady(true);
    // GPIO47 is the keyboard backlight's SY7200A PWM/enable pin.  Arm it at
    // zero duty immediately after detection, matching Meck's safe boot-off
    // state instead of leaving the pin unconfigured until the LilyGO key.
    board.keyboardSetBrightness(0);
    ESP_LOGI(kTag,
             "P4 keyboard ready P2=(sda=%d,scl=%d,int=%d,backlight=%d) XL9555=0x%02X TCA8418=0x%02X matrix=%dx%d poll=%lums",
             keyboard.sda,
             keyboard.scl,
             keyboard.interrupt,
             keyboard.backlight,
             static_cast<unsigned>(keyboard.xl9555),
             static_cast<unsigned>(keyboard.tca8418),
             keyboard.columns,
             keyboard.rows,
             static_cast<unsigned long>(kPollIntervalMs));
    return true;
}

uint32_t translate_key(uint8_t key_number)
{
    if (key_number == 0 || key_number > kKeyMap.size())
    {
        return 0;
    }

    const uint32_t base = kKeyMap[key_number - 1U];
    switch (base)
    {
    case kKeyCaps:
        s_caps_lock = !s_caps_lock;
        return 0;
    case kKeyShift:
        s_shift = true;
        return 0;
    case kKeyFn:
        s_fn = true;
        return 0;
    case kKeyWin:
    {
        auto& board = boards::t_display_p4::TDisplayP4Board::instance();
        const uint8_t current = board.keyboardGetBrightness();
        const uint8_t next = current == 0 ? DEVICE_MAX_BRIGHTNESS_LEVEL : 0;
        board.keyboardSetBrightness(next);
        return 0;
    }
    case kKeyAlt:
    case kKeyCtrl:
    case kKeyF11:
    case kKeyRecord:
        return 0;
    default:
        break;
    }

    if (base >= 0x81 && base <= 0x8A)
    {
        return 0;
    }

    if (s_fn)
    {
        s_fn = false;
        s_shift = false;
        const uint32_t shifted = kShiftKeyMap[key_number - 1U];
        return shifted >= 0x20 && shifted <= 0x7E ? shifted : 0;
    }

    if (base >= 'a' && base <= 'z')
    {
        const bool upper = s_shift != s_caps_lock;
        s_shift = false;
        return upper ? base - 'a' + 'A' : base;
    }

    if (s_shift)
    {
        s_shift = false;
        const uint32_t shifted = kShiftKeyMap[key_number - 1U];
        if (shifted >= 0x20 && shifted <= 0x7E)
        {
            return shifted;
        }
    }
    return base;
}

void enqueue_key(uint32_t key)
{
    if (key == 0 || s_event_queue_count == s_event_queue.size())
    {
        return;
    }

    const size_t tail = (s_event_queue_head + s_event_queue_count) % s_event_queue.size();
    s_event_queue[tail] = key;
    ++s_event_queue_count;
}

uint32_t dequeue_key()
{
    if (s_event_queue_count == 0)
    {
        return 0;
    }

    const uint32_t key = s_event_queue[s_event_queue_head];
    s_event_queue_head = (s_event_queue_head + 1U) % s_event_queue.size();
    --s_event_queue_count;
    return key;
}

bool key_is_text_input(uint32_t key)
{
    return (key >= 0x20U && key <= 0x7EU) ||
           key == LV_KEY_BACKSPACE ||
           key == LV_KEY_ENTER ||
           key == LV_KEY_LEFT ||
           key == LV_KEY_RIGHT ||
           key == LV_KEY_UP ||
           key == LV_KEY_DOWN ||
           key == LV_KEY_HOME ||
           key == LV_KEY_END ||
           key == LV_KEY_ESC;
}

lv_obj_t* find_focused_textarea(lv_obj_t* root)
{
    if (root == nullptr || !lv_obj_is_valid(root) || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN))
    {
        return nullptr;
    }
    if (lv_obj_check_type(root, &lv_textarea_class) &&
        lv_obj_has_state(root, LV_STATE_FOCUSED))
    {
        return root;
    }

    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t index = 0; index < count; ++index)
    {
        if (lv_obj_t* textarea = find_focused_textarea(lv_obj_get_child(root, index)))
        {
            return textarea;
        }
    }
    return nullptr;
}

struct TextareaSearch
{
    lv_obj_t* only = nullptr;
    uint32_t count = 0;
};

void find_visible_textareas(lv_obj_t* root, TextareaSearch* result)
{
    if (root == nullptr || result == nullptr || !lv_obj_is_valid(root) ||
        lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN) || result->count > 1)
    {
        return;
    }
    if (lv_obj_check_type(root, &lv_textarea_class))
    {
        result->only = root;
        ++result->count;
        return;
    }

    const uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t index = 0; index < count; ++index)
    {
        find_visible_textareas(lv_obj_get_child(root, index), result);
    }
}

bool route_to_textarea(uint32_t key)
{
    if (!key_is_text_input(key))
    {
        return false;
    }

    lv_obj_t* textarea = find_focused_textarea(lv_screen_active());
    if (textarea == nullptr && key >= 0x20U && key <= 0x7EU)
    {
        TextareaSearch search{};
        find_visible_textareas(lv_screen_active(), &search);
        if (search.count == 1)
        {
            textarea = search.only;
            lv_obj_add_state(textarea, LV_STATE_FOCUSED);
            lv_obj_send_event(textarea, LV_EVENT_FOCUSED, nullptr);
        }
    }
    if (textarea == nullptr)
    {
        return false;
    }

    if (key == LV_KEY_ESC)
    {
        return lv_obj_send_event(textarea, LV_EVENT_CANCEL, nullptr) == LV_RESULT_OK;
    }
    uint32_t parameter = key;
    return lv_obj_send_event(textarea, LV_EVENT_KEY, &parameter) == LV_RESULT_OK;
}

bool route_to_group(uint32_t key)
{
    lv_group_t* group = ui_get_active_app() != nullptr ? app_g : menu_g;
    if (group == nullptr || lv_group_get_focused(group) == nullptr)
    {
        return false;
    }
    return lv_group_send_data(group, key) == LV_RESULT_OK;
}

bool route_to_screen(uint32_t key)
{
    lv_obj_t* const screen = lv_screen_active();
    if (screen == nullptr || !lv_obj_is_valid(screen) || lv_obj_get_event_count(screen) == 0)
    {
        return false;
    }

    uint32_t parameter = key;
    (void)lv_obj_send_event(screen, LV_EVENT_KEY, &parameter);
    return true;
}

void route_key(uint32_t key)
{
    if (key == 0)
    {
        return;
    }

    lv_display_trigger_activity(nullptr);
    trail_mate_idf_note_user_activity();

    // The menu has established physical-key shortcuts. Keep them P4-local at
    // the adapter boundary and only while the menu is actually active.
    if (ui_get_active_app() == nullptr && key <= 0x7F)
    {
        const char character = static_cast<char>(key);
        if (ui::menu_runtime::handleWalkieKey(character, 1) ||
            ui::menu_runtime::handleShortcutKey(character, 1))
        {
            return;
        }
    }

    if (route_to_textarea(key))
    {
        return;
    }

    // ESC is the P4's unambiguous app-exit key. It must not disappear into a
    // focused LVGL group that has no ESC callback, which was the terminal
    // failure mode of the former generic keypad-device integration.
    if (key == LV_KEY_ESC && ui_get_active_app() != nullptr)
    {
        ui_request_exit_to_menu();
        return;
    }

    if (route_to_group(key))
    {
        return;
    }

    // Several Trail pages intentionally register their root as the keyboard
    // target instead of adding it to app_g. This direct, same-thread route
    // preserves that contract without reviving a global default-group input.
    if (route_to_screen(key))
    {
        return;
    }
}

void note_i2c_failure(const char* operation)
{
    const uint32_t now = lv_tick_get();
    s_next_i2c_attempt_ms = now + kI2cFailureBackoffMs;
    if (s_last_i2c_failure_log_ms == 0 ||
        static_cast<uint32_t>(now - s_last_i2c_failure_log_ms) >= kI2cFailureLogIntervalMs)
    {
        s_last_i2c_failure_log_ms = now;
        ESP_LOGW(kTag, "P4 keyboard %s failed; retrying after %lums", operation,
                 static_cast<unsigned long>(kI2cFailureBackoffMs));
    }
}

void drain_fifo()
{
    const uint32_t now = lv_tick_get();
    if (static_cast<int32_t>(now - s_next_i2c_attempt_ms) < 0)
    {
        return;
    }

    const auto& keyboard = keyboard_module();
    uint8_t irq_status = 0;
    uint8_t count_reg = 0;
    if (!read_register(keyboard.tca8418, kTca8418RegIntStat, &irq_status) ||
        !read_register(keyboard.tca8418, kTca8418RegKeyLockEventCount, &count_reg))
    {
        note_i2c_failure("TCA8418 FIFO read");
        return;
    }

    const uint8_t event_count = std::min<uint8_t>(count_reg & 0x0F, kTca8418MaxKeyEvents);
    if (event_count == 0)
    {
        if (irq_status != 0)
        {
            (void)write_register(keyboard.tca8418, kTca8418RegIntStat, irq_status);
        }
        return;
    }

    uint8_t events[kTca8418MaxKeyEvents] = {};
    if (!read_registers(keyboard.tca8418, kTca8418RegKeyEventA, events, event_count))
    {
        note_i2c_failure("TCA8418 event FIFO transfer");
        return;
    }
    (void)write_register(keyboard.tca8418,
                         kTca8418RegIntStat,
                         static_cast<uint8_t>(irq_status | kTca8418IntKeyEvents));

    for (uint8_t index = 0; index < event_count; ++index)
    {
        // The controller reports both edges. Queue only press edges, and do
        // not route them here: a route may replace the active page tree, so
        // the timer must deliver at most one semantic action per turn.
        if ((events[index] & 0x80U) == 0)
        {
            continue;
        }
        enqueue_key(translate_key(events[index] & 0x7FU));
    }
}

void poll_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (!s_ready)
    {
        return;
    }

    uint32_t key = dequeue_key();
    if (key != 0)
    {
        route_key(key);
        return;
    }

    drain_fifo();
    key = dequeue_key();
    if (key != 0)
    {
        route_key(key);
    }
}

} // namespace

extern "C" bool trail_mate_t_display_p4_keyboard_start(void)
{
    if (s_poll_timer != nullptr)
    {
        return s_ready;
    }

    if (!boards::t_display_p4::TDisplayP4Board::profile().supports_keyboard_module)
    {
        return false;
    }

    boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
    s_ready = initialize_controller();
    if (!s_ready)
    {
        return false;
    }

    s_poll_timer = lv_timer_create(poll_timer_cb, kPollIntervalMs, nullptr);
    if (s_poll_timer == nullptr)
    {
        boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
        s_ready = false;
        ESP_LOGW(kTag, "P4 keyboard could not create the LVGL polling timer");
        return false;
    }
    return true;
}

extern "C" bool trail_mate_t_display_p4_keyboard_is_ready(void)
{
    return s_ready;
}
