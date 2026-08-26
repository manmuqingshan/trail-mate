#include "bsp/trail_mate_t_display_p4_keyboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "bus/i2c/software_i2c.h"
#include "chip/i2c/tca8418.h"
#include "chip/i2c/xl95x5.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/ui_common.h"

extern "C" void trail_mate_idf_note_user_activity(void);

namespace
{

constexpr const char* kTag = "t-display-p4-kbd";

// P2 is a dedicated, bit-banged two-device bus on the K270 keyboard. It is
// not the board SYS-I2C or external I2C bus, so this adapter owns its two
// LilyGO SoftwareI2c instances for the lifetime of the P4 keyboard.
constexpr uint32_t kPowerSettleMs = 50;
constexpr uint32_t kResetDelayMs = 10;
constexpr uint32_t kPollIntervalMs = 30;
constexpr uint32_t kI2cFailureBackoffMs = 500;
constexpr uint32_t kI2cFailureLogIntervalMs = 2000;
constexpr uint8_t kTca8418FifoDepth = 10;
constexpr uint32_t kAltDoublePressMs = 350;

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
bool s_initialization_attempted = false;
bool s_caps_lock = false;
bool s_shift = false;
bool s_fn = false;
lv_timer_t* s_poll_timer = nullptr;
uint32_t s_next_i2c_attempt_ms = 0;
uint32_t s_last_i2c_failure_log_ms = 0;
uint32_t s_last_alt_press_ms = 0;
std::array<uint32_t, kEventQueueCapacity> s_event_queue{};
size_t s_event_queue_head = 0;
size_t s_event_queue_count = 0;
std::shared_ptr<cpp_bus_driver::SoftwareI2c> s_xl9555_bus;
std::shared_ptr<cpp_bus_driver::SoftwareI2c> s_tca8418_bus;
std::unique_ptr<cpp_bus_driver::Xl95x5> s_xl9555;
std::unique_ptr<cpp_bus_driver::Tca8418> s_tca8418;

const boards::t_display_p4::BoardProfile::KeyboardModule& keyboard_module()
{
    return boards::t_display_p4::TDisplayP4Board::profile().keyboard;
}

void reset_vendor_driver_state()
{
    s_tca8418.reset();
    s_xl9555.reset();
    s_tca8418_bus.reset();
    s_xl9555_bus.reset();
}

bool reset_tca8418_via_xl9555()
{
    using Xl95x5 = cpp_bus_driver::Xl95x5;
    if (s_xl9555 == nullptr ||
        !s_xl9555->SetGpioMode(Xl95x5::Pin::kIo6, Xl95x5::Mode::kOutput) ||
        !s_xl9555->GpioWrite(Xl95x5::Pin::kIo6, 1))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));

    if (!s_xl9555->GpioWrite(Xl95x5::Pin::kIo6, 0))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kResetDelayMs));

    if (!s_xl9555->GpioWrite(Xl95x5::Pin::kIo6, 1))
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

    reset_vendor_driver_state();
    s_xl9555_bus = std::make_shared<cpp_bus_driver::SoftwareI2c>(keyboard.sda, keyboard.scl);
    s_tca8418_bus = std::make_shared<cpp_bus_driver::SoftwareI2c>(keyboard.sda, keyboard.scl);
    s_xl9555 = std::make_unique<cpp_bus_driver::Xl95x5>(s_xl9555_bus, keyboard.xl9555);
    s_tca8418 = std::make_unique<cpp_bus_driver::Tca8418>(s_tca8418_bus, keyboard.tca8418);

    // The device protocol is not reimplemented here. SoftwareI2c, Xl95x5,
    // Tca8418::Init(), matrix setup, FIFO decoding, and IRQ clearing are all
    // LilyGO cpp_bus_driver calls. This file only binds that vendor driver to
    // P4 LDO/reset wiring and Trail's event route.
    if (!s_xl9555->Init())
    {
        ESP_LOGI(kTag, "P4 keyboard not attached: LilyGO XL9555 0x%02X did not ACK",
                 static_cast<unsigned>(keyboard.xl9555));
        reset_vendor_driver_state();
        return false;
    }
    if (!reset_tca8418_via_xl9555())
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO XL9555 reset sequence failed");
        reset_vendor_driver_state();
        return false;
    }
    if (!s_tca8418->Init())
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO TCA8418 0x%02X did not ACK after reset",
                 static_cast<unsigned>(keyboard.tca8418));
        reset_vendor_driver_state();
        return false;
    }
    if (!s_tca8418->SetKeypadScanWindow(0, 0, keyboard.columns, keyboard.rows) ||
        !s_tca8418->SetInterruptEnable(cpp_bus_driver::Tca8418::IrqMask::kKeyEvents) ||
        !s_tca8418->ClearIrqFlag(cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents))
    {
        ESP_LOGW(kTag, "P4 keyboard LilyGO TCA8418 scan setup failed");
        reset_vendor_driver_state();
        return false;
    }

    s_caps_lock = false;
    s_shift = false;
    s_fn = false;
    s_event_queue_head = 0;
    s_event_queue_count = 0;
    s_next_i2c_attempt_ms = 0;
    s_last_i2c_failure_log_ms = 0;
    s_last_alt_press_ms = 0;
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
    {
        const uint32_t now = lv_tick_get();
        if (s_last_alt_press_ms != 0 &&
            static_cast<uint32_t>(now - s_last_alt_press_ms) <= kAltDoublePressMs)
        {
            ui_take_screenshot_to_sd();
            s_last_alt_press_ms = 0;
            return 0;
        }
        s_last_alt_press_ms = now;
        return 0;
    }
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

    if (s_tca8418 == nullptr)
    {
        note_i2c_failure("LilyGO TCA8418 driver state");
        return;
    }

    // Do not read TCA8418 registers in Trail. The vendor driver's API owns
    // FIFO count, event decoding, and interrupt acknowledgement semantics.
    const uint8_t reported_count = s_tca8418->GetFingerCount();
    if (reported_count == 0xFFU)
    {
        note_i2c_failure("LilyGO TCA8418 FIFO count");
        return;
    }

    if (reported_count == 0)
    {
        return;
    }
    if (reported_count > kTca8418FifoDepth)
    {
        note_i2c_failure("LilyGO TCA8418 invalid FIFO count");
        return;
    }

    for (uint8_t index = 0; index < reported_count; ++index)
    {
        cpp_bus_driver::Tca8418::TouchInfo event{};
        if (!s_tca8418->ReadKeyEvent(&event))
        {
            note_i2c_failure("LilyGO TCA8418 event FIFO transfer");
            return;
        }

        // The controller reports both edges. Queue only press edges, and do
        // not route them here: a route may replace the active page tree, so
        // the timer must deliver at most one semantic action per turn.
        if (event.event_type != cpp_bus_driver::Tca8418::EventType::kKeypad ||
            !event.press_flag)
        {
            continue;
        }
        enqueue_key(translate_key(event.num));
    }

    if (!s_tca8418->ClearIrqFlag(cpp_bus_driver::Tca8418::IrqFlag::kKeyEvents))
    {
        note_i2c_failure("LilyGO TCA8418 IRQ acknowledgement");
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

extern "C" bool trail_mate_t_display_p4_keyboard_initialize(void)
{
    if (s_ready)
    {
        return true;
    }

    if (!boards::t_display_p4::TDisplayP4Board::profile().supports_keyboard_module)
    {
        return false;
    }

    // Do not silently fall back to probing from the running LVGL task.  The
    // official P4 keyboard example performs this 5 us software-I2C exchange
    // before it starts LVGL, and a failed early attach must remain observable
    // rather than being retried later under the display workload.
    if (s_initialization_attempted)
    {
        return false;
    }

    s_initialization_attempted = true;
    boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
    s_ready = initialize_controller();
    return s_ready;
}

extern "C" bool trail_mate_t_display_p4_keyboard_start(void)
{
    if (s_poll_timer != nullptr)
    {
        return s_ready;
    }

    if (!trail_mate_t_display_p4_keyboard_initialize())
    {
        return false;
    }

    s_poll_timer = lv_timer_create(poll_timer_cb, kPollIntervalMs, nullptr);
    if (s_poll_timer == nullptr)
    {
        boards::t_display_p4::TDisplayP4Board::instance().setKeyboardReady(false);
        s_ready = false;
        s_initialization_attempted = false;
        reset_vendor_driver_state();
        ESP_LOGW(kTag, "P4 keyboard could not create the LVGL polling timer");
        return false;
    }
    return true;
}

extern "C" bool trail_mate_t_display_p4_keyboard_is_ready(void)
{
    return s_ready;
}
