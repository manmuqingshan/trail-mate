/**
 * @file screen_sleep.cpp
 * @brief Shared screen sleep timeout/runtime helpers.
 */

#include "screen_sleep.h"

#include "board/BoardBase.h"
#include "display/DisplayConfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"

namespace
{
constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000;
constexpr uint32_t kScreenTimeoutMaxMs = 300000;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000;
constexpr uint32_t kScreenTimeoutMaxBleSecs = 900;
constexpr uint32_t kScreenSaverDurationMs = 3000;
constexpr uint32_t kScreenSleepTaskStackBytes = 4 * 1024;

ScreenSleepHooks s_hooks{};

portMUX_TYPE s_screen_timeout_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_screen_sleep_timeout_ms = kScreenTimeoutDefaultMs;
bool s_screen_sleep_timeout_loaded = false;

SemaphoreHandle_t s_activity_mutex = nullptr;
TaskHandle_t s_screen_sleep_task_handle = nullptr;
uint32_t s_last_user_activity_time = 0;
bool s_screen_sleeping = false;
uint32_t s_screen_sleep_disable_depth = 0;
uint8_t s_saved_screen_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;
uint8_t s_saved_keyboard_brightness = 127;
bool s_screen_saver_active = false;
lv_timer_t* s_screen_saver_timer = nullptr;

enum class ScreenHardwareActionKind : uint8_t
{
    None,
    EnterSleep,
    ExitSleep,
};

struct ScreenHardwareAction
{
    ScreenHardwareActionKind kind = ScreenHardwareActionKind::None;
    uint8_t screen_brightness = 0;
    uint8_t keyboard_brightness = 0;
    bool has_keyboard = false;
};

void apply_screen_hardware_action(const ScreenHardwareAction& action)
{
    switch (action.kind)
    {
    case ScreenHardwareActionKind::EnterSleep:
        if (action.has_keyboard)
        {
            board.keyboardSetBrightness(0);
        }
        board.setBrightness(0);
        board.enterScreenSleep();
        break;
    case ScreenHardwareActionKind::ExitSleep:
        board.exitScreenSleep();
        board.setBrightness(action.screen_brightness);
        if (action.has_keyboard)
        {
            board.keyboardSetBrightness(action.keyboard_brightness);
        }
        break;
    case ScreenHardwareActionKind::None:
        break;
    }
}

uint32_t readPersistedScreenTimeoutMs()
{
    const uint32_t value =
        ::platform::ui::settings_store::get_uint(kSettingsNs, kScreenTimeoutKey, 0);
    return clampScreenTimeoutMs(value);
}

void writePersistedScreenTimeoutMs(uint32_t timeout_ms)
{
    ::platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, timeout_ms);
}

void cacheScreenTimeoutMs(uint32_t timeout_ms)
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    s_screen_sleep_timeout_ms = timeout_ms;
    s_screen_sleep_timeout_loaded = true;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
}

bool isScreenTimeoutLoaded()
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    const bool loaded = s_screen_sleep_timeout_loaded;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
    return loaded;
}

uint32_t cachedScreenTimeoutMs()
{
    taskENTER_CRITICAL(&s_screen_timeout_mux);
    const uint32_t timeout_ms = s_screen_sleep_timeout_ms;
    taskEXIT_CRITICAL(&s_screen_timeout_mux);
    return timeout_ms;
}

void screen_saver_timer_cb(lv_timer_t* timer);

void show_screen_saver_layer()
{
    if (s_hooks.show_screen_saver)
    {
        s_hooks.show_screen_saver();
    }
}

void present_screen_saver_layer()
{
    if (s_hooks.present_screen_saver)
    {
        s_hooks.present_screen_saver();
    }
}

void restart_screen_saver_timer()
{
    if (s_screen_saver_timer == nullptr)
    {
        s_screen_saver_timer = lv_timer_create(screen_saver_timer_cb, kScreenSaverDurationMs, nullptr);
    }
    else
    {
        lv_timer_set_period(s_screen_saver_timer, kScreenSaverDurationMs);
        lv_timer_reset(s_screen_saver_timer);
        lv_timer_resume(s_screen_saver_timer);
    }
}

void notifyWakeFromSleep()
{
    if (s_hooks.on_wake_from_sleep)
    {
        s_hooks.on_wake_from_sleep();
    }
}

void hide_screen_saver_layer()
{
    if (s_hooks.hide_screen_saver) s_hooks.hide_screen_saver();
    if (s_screen_saver_timer)
    {
        lv_timer_pause(s_screen_saver_timer);
    }
}

void refresh_active_screen()
{
    lv_obj_t* active = lv_screen_active();
    if (active == nullptr)
    {
        return;
    }
    lv_obj_invalidate(active);
}

void screen_saver_timer_cb(lv_timer_t* /*timer*/)
{
    ScreenHardwareAction action{};
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            s_screen_saver_active = false;
            s_screen_sleeping = true;
            action.kind = ScreenHardwareActionKind::EnterSleep;
            action.has_keyboard = board.hasKeyboard();
            xSemaphoreGive(s_activity_mutex);
        }
    }
    hide_screen_saver_layer();
    apply_screen_hardware_action(action);
}

void screenSleepTask(void* pvParameters)
{
    (void)pvParameters;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t check_interval = pdMS_TO_TICKS(1000);

    while (true)
    {
        ScreenHardwareAction action{};
        bool sample_sleep_brightness = false;
        const uint32_t current_time = millis();
        const uint32_t current_timeout = getScreenSleepTimeout();

        if (s_activity_mutex != nullptr)
        {
            if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
            {
                const uint32_t time_since_activity = current_time - s_last_user_activity_time;
                const bool sleep_disabled = s_screen_sleep_disable_depth > 0;
                if (sleep_disabled)
                {
                    if (s_screen_sleeping && !s_screen_saver_active)
                    {
                        s_screen_sleeping = false;
                        action.kind = ScreenHardwareActionKind::ExitSleep;
                        action.screen_brightness = s_saved_screen_brightness;
                        action.keyboard_brightness = s_saved_keyboard_brightness;
                        action.has_keyboard = board.hasKeyboard();
                    }
                }
                else if (!s_screen_sleeping && time_since_activity >= current_timeout)
                {
                    sample_sleep_brightness = true;
                }
                xSemaphoreGive(s_activity_mutex);
            }
        }

        if (sample_sleep_brightness)
        {
            const bool has_keyboard = board.hasKeyboard();
            const uint8_t screen_brightness = board.getBrightness();
            const uint8_t keyboard_brightness =
                has_keyboard ? board.keyboardGetBrightness() : 0;

            if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
            {
                const uint32_t verified_time_since_activity =
                    millis() - s_last_user_activity_time;
                const bool still_sleep_eligible =
                    s_screen_sleep_disable_depth == 0 &&
                    !s_screen_sleeping &&
                    verified_time_since_activity >= getScreenSleepTimeout();
                if (still_sleep_eligible)
                {
                    s_screen_sleeping = true;
                    s_saved_screen_brightness = screen_brightness;
                    s_saved_keyboard_brightness = keyboard_brightness;
                    action.kind = ScreenHardwareActionKind::EnterSleep;
                    action.has_keyboard = has_keyboard;
                }
                xSemaphoreGive(s_activity_mutex);
            }
        }

        apply_screen_hardware_action(action);
        vTaskDelayUntil(&last_wake_time, check_interval);
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    if (timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutMaxMs;
    }
    return timeout_ms;
}

uint32_t getScreenSleepTimeout()
{
    if (!isScreenTimeoutLoaded())
    {
        cacheScreenTimeoutMs(readPersistedScreenTimeoutMs());
    }
    return cachedScreenTimeoutMs();
}

uint16_t readScreenTimeoutSecs()
{
    uint32_t secs = getScreenSleepTimeout() / 1000U;
    if (secs > kScreenTimeoutMaxBleSecs)
    {
        secs = kScreenTimeoutMaxBleSecs;
    }
    return static_cast<uint16_t>(secs);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    timeout_ms = clampScreenTimeoutMs(timeout_ms);
    writePersistedScreenTimeoutMs(timeout_ms);
    cacheScreenTimeoutMs(timeout_ms);
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    s_hooks = hooks;

    if (s_activity_mutex == nullptr)
    {
        s_activity_mutex = xSemaphoreCreateMutex();
        if (s_activity_mutex == nullptr)
        {
            log_e("Failed to create activity mutex");
            return;
        }

        s_last_user_activity_time = millis();
        (void)getScreenSleepTimeout();
    }

    if (s_screen_sleep_task_handle == nullptr)
    {
        BaseType_t sleep_task_result = xTaskCreate(
            screenSleepTask,
            "screen_sleep",
            kScreenSleepTaskStackBytes,
            nullptr,
            3,
            &s_screen_sleep_task_handle);
        if (sleep_task_result != pdPASS)
        {
            log_e("Failed to create screen sleep task");
            s_screen_sleep_task_handle = nullptr;
        }
        else
        {
            log_d("Screen sleep management task created successfully");
        }
    }
}

bool isScreenSleeping()
{
    bool sleeping = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            sleeping = s_screen_sleeping;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return sleeping;
}

bool isScreenSaverActive()
{
    bool active = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            active = s_screen_saver_active;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return active;
}

void wakeScreenSaver()
{
    if (s_activity_mutex == nullptr)
    {
        return;
    }

    bool was_sleeping = false;
    if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
    {
        if (s_screen_sleep_disable_depth > 0)
        {
            s_last_user_activity_time = millis();
            xSemaphoreGive(s_activity_mutex);
            return;
        }

        was_sleeping = s_screen_sleeping;
        s_screen_saver_active = true;
        // Keep the runtime in the logical sleep state while the transient
        // saver is visible. This matches the 0.1.13 behavior: waking only
        // shows the saver shell, and if the user does nothing for 3s we go
        // right back to sleep instead of being treated as fully awake.
        s_screen_sleeping = true;
        xSemaphoreGive(s_activity_mutex);
    }

    if (was_sleeping)
    {
        board.exitScreenSleep();
        board.setBrightness(0);
        if (board.hasKeyboard())
        {
            board.keyboardSetBrightness(0);
        }
    }
    else
    {
        s_saved_screen_brightness = board.getBrightness();
    }
    show_screen_saver_layer();
    present_screen_saver_layer();

    if (was_sleeping)
    {
        board.setBrightness(s_saved_screen_brightness);
        if (board.hasKeyboard())
        {
            board.keyboardSetBrightness(s_saved_keyboard_brightness);
        }
    }

    restart_screen_saver_timer();
}

void enterFromScreenSaver()
{
    if (!isScreenSaverActive())
    {
        return;
    }

    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s_screen_saver_active = false;
            s_screen_sleeping = false;
            s_last_user_activity_time = millis();
            xSemaphoreGive(s_activity_mutex);
        }
    }

    hide_screen_saver_layer();
    refresh_active_screen();
    updateUserActivity();
}

void wakeScreenForModal()
{
    enterFromScreenSaver();
}

void disableScreenSleep()
{
    bool hide_saver = false;
    ScreenHardwareAction action{};
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            const bool was_disabled = s_screen_sleep_disable_depth > 0;
            if (s_screen_sleep_disable_depth < UINT32_MAX)
            {
                ++s_screen_sleep_disable_depth;
            }

            if (!was_disabled && s_screen_saver_active)
            {
                s_screen_saver_active = false;
                hide_saver = true;
            }
            if (!was_disabled && s_screen_sleeping)
            {
                s_screen_sleeping = false;
                action.kind = ScreenHardwareActionKind::ExitSleep;
                action.screen_brightness = s_saved_screen_brightness;
                action.keyboard_brightness = s_saved_keyboard_brightness;
                action.has_keyboard = board.hasKeyboard();
            }
            xSemaphoreGive(s_activity_mutex);
        }
    }
    if (hide_saver)
    {
        hide_screen_saver_layer();
    }
    apply_screen_hardware_action(action);
}

void enableScreenSleep()
{
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            if (s_screen_sleep_disable_depth > 0)
            {
                --s_screen_sleep_disable_depth;
                if (s_screen_sleep_disable_depth == 0)
                {
                    s_last_user_activity_time = millis();
                }
            }
            xSemaphoreGive(s_activity_mutex);
        }
    }
}

bool isScreenSleepDisabled()
{
    bool disabled = false;
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            disabled = s_screen_sleep_disable_depth > 0;
            xSemaphoreGive(s_activity_mutex);
        }
    }
    return disabled;
}

void updateUserActivity()
{
    bool woke_from_sleep = false;
    bool hide_saver = false;
    bool restore_sleep_state = false;
    ScreenHardwareAction action{};
    if (s_activity_mutex != nullptr)
    {
        if (xSemaphoreTake(s_activity_mutex, portMAX_DELAY) == pdTRUE)
        {
            s_last_user_activity_time = millis();
            if (s_screen_saver_active)
            {
                s_screen_saver_active = false;
                hide_saver = true;
            }
            if (s_screen_sleeping)
            {
                s_screen_saver_active = true;
                woke_from_sleep = true;
                restore_sleep_state = true;
                action.kind = ScreenHardwareActionKind::ExitSleep;
                action.screen_brightness = s_saved_screen_brightness;
                action.keyboard_brightness = s_saved_keyboard_brightness;
                action.has_keyboard = board.hasKeyboard();
            }
            xSemaphoreGive(s_activity_mutex);
        }
    }
    if (hide_saver)
    {
        hide_screen_saver_layer();
        refresh_active_screen();
    }
    if (restore_sleep_state)
    {
        apply_screen_hardware_action(action);
    }
    if (woke_from_sleep)
    {
        notifyWakeFromSleep();
        present_screen_saver_layer();
        restart_screen_saver_timer();
    }
}

// ===================================================================
// platform::ui::screen contract
// ===================================================================

namespace platform::ui::screen
{
namespace
{

ScreenSleepHooks adapt_hooks(const Hooks& hooks)
{
    ScreenSleepHooks adapted{};
    adapted.format_time = hooks.format_time;
    adapted.read_unread_count = hooks.read_unread_count;
    adapted.show_main_menu = hooks.show_main_menu;
    adapted.on_wake_from_sleep = hooks.on_wake_from_sleep;
    adapted.show_screen_saver = hooks.show_screen_saver;
    adapted.hide_screen_saver = hooks.hide_screen_saver;
    adapted.present_screen_saver = hooks.present_screen_saver;
    return adapted;
}

} // namespace

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return clampScreenTimeoutMs(timeout_ms);
}

uint32_t timeout_ms()
{
    return getScreenSleepTimeout();
}

uint16_t timeout_secs()
{
    return readScreenTimeoutSecs();
}

bool supports_app_timeout_setting()
{
    return true;
}

void set_timeout_ms(uint32_t timeout_ms)
{
    setScreenSleepTimeout(timeout_ms);
}

void init(const Hooks& hooks)
{
    initScreenSleepRuntime(adapt_hooks(hooks));
}

bool is_sleeping()
{
    return isScreenSleeping();
}

bool is_sleep_disabled()
{
    return isScreenSleepDisabled();
}

bool is_saver_active()
{
    return isScreenSaverActive();
}

void wake_saver()
{
    wakeScreenSaver();
}

void enter_from_saver()
{
    enterFromScreenSaver();
}

void wake_for_modal()
{
    wakeScreenForModal();
}

void update_user_activity()
{
    updateUserActivity();
}

void disable_sleep()
{
    disableScreenSleep();
}

void enable_sleep()
{
    enableScreenSleep();
}

} // namespace platform::ui::screen
