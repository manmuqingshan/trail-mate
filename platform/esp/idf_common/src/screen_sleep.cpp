/**
 * @file screen_sleep.cpp
 * @brief ESP-IDF screen sleep runtime backed by shared settings and BSP display controls.
 */

#include "screen_sleep.h"

#include <cstdint>

#include "board/BoardBase.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/ui_dispatcher.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/settings_store.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
extern "C"
{
#include "bsp/trail_mate_tab5_runtime.h"
}
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/runtime_support.h"
#endif

namespace
{

constexpr const char* kTag = "idf-screen-sleep";
constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr uint32_t kScreenTimeoutMinMs = 10000;
constexpr uint32_t kScreenTimeoutMaxMs = 300000;
constexpr uint32_t kScreenTimeoutDefaultMs = 60000;
constexpr uint32_t kScreenTimeoutMaxBleSecs = 900;
constexpr uint32_t kTaskPeriodMs = 250;
constexpr uint32_t kScreenSaverDurationMs = 3000;

SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task = nullptr;
uint32_t s_timeout_ms = kScreenTimeoutDefaultMs;
bool s_timeout_loaded = false;
uint32_t s_last_user_activity_ms = 0;
bool s_screen_sleeping = false;
bool s_screen_sleep_disabled = false;
bool s_screen_saver_active = false;
uint32_t s_screen_saver_started_ms = 0;
uint8_t s_saved_screen_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL;

bool auto_sleep_supported()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // P4 touch wake is still a board bring-up contract. Keep the UI awake so
    // field LoRa/debug sessions do not look frozen after the backlight sleeps.
    return false;
#else
    return true;
#endif
}

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t clamp_timeout_internal(uint32_t timeout_ms)
{
    if (timeout_ms < kScreenTimeoutMinMs || timeout_ms > kScreenTimeoutMaxMs)
    {
        return kScreenTimeoutDefaultMs;
    }
    return timeout_ms;
}

void ensure_mutex()
{
    if (s_mutex == nullptr)
    {
        s_mutex = xSemaphoreCreateMutex();
    }
}

void load_timeout_if_needed_locked()
{
    if (s_timeout_loaded)
    {
        return;
    }
    s_timeout_ms = clamp_timeout_internal(
        platform::ui::settings_store::get_uint(kSettingsNs, kScreenTimeoutKey, kScreenTimeoutDefaultMs));
    s_timeout_loaded = true;
}

void restore_display_hardware_locked()
{
    platform::ui::device::set_screen_brightness(s_saved_screen_brightness);
    s_last_user_activity_ms = now_ms();
    ESP_LOGI(kTag, "Display wake");
}

void wake_screen_saver_locked()
{
    restore_display_hardware_locked();
    s_screen_sleeping = true;
    s_screen_saver_active = true;
    s_screen_saver_started_ms = now_ms();
}

void enter_ui_locked()
{
    restore_display_hardware_locked();
    s_screen_sleeping = false;
    s_screen_saver_active = false;
    s_screen_saver_started_ms = 0;
}

void sleep_display_locked()
{
    s_saved_screen_brightness = platform::ui::device::screen_brightness();
    platform::esp::idf_common::bsp_runtime::sleep_display();
    s_screen_sleeping = true;
    s_screen_saver_active = false;
    ESP_LOGI(kTag, "Display sleep");
}

void notify_wake()
{
    // Post to UI dispatcher instead of calling LVGL hooks directly.
    // The drain timer runs in LVGL task context where it is safe.
    platform::esp::idf_common::ui_dispatcher::post(
        platform::esp::idf_common::ui_dispatcher::Event::WakeFromSleep);
}

void notify_hide_saver()
{
    platform::esp::idf_common::ui_dispatcher::post(
        platform::esp::idf_common::ui_dispatcher::Event::HideScreenSaver);
}

bool wake_requested_by_touch_irq_locked()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return s_screen_sleeping && trail_mate_tab5_touch_interrupt_active();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return s_screen_sleeping && boards::t_display_p4::runtime_support::touch_interrupt_active();
#else
    return false;
#endif
}

void screen_sleep_task(void*)
{
    while (true)
    {
        bool should_notify_wake = false;
        bool should_hide_saver = false;
        ensure_mutex();
        if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
        {
            load_timeout_if_needed_locked();
            const uint32_t now = now_ms();
            const uint32_t elapsed = now - s_last_user_activity_ms;
            const bool saver_expired =
                s_screen_saver_active && (now - s_screen_saver_started_ms >= kScreenSaverDurationMs);
            if (s_screen_sleep_disabled)
            {
                if (s_screen_sleeping && !s_screen_saver_active)
                {
                    wake_screen_saver_locked();
                    should_notify_wake = true;
                }
            }
            else if (saver_expired)
            {
                sleep_display_locked();
                should_hide_saver = true;
            }
            else if (!s_screen_saver_active && wake_requested_by_touch_irq_locked())
            {
                ESP_LOGI(kTag, "Display wake requested by board touch interrupt");
                wake_screen_saver_locked();
                should_notify_wake = true;
            }
            else if (auto_sleep_supported() && (s_screen_sleeping == false) && elapsed >= s_timeout_ms)
            {
                sleep_display_locked();
            }
            xSemaphoreGive(s_mutex);
        }
        if (should_notify_wake)
        {
            notify_wake();
        }
        if (should_hide_saver)
        {
            notify_hide_saver();
        }
        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    return clamp_timeout_internal(timeout_ms);
}
uint32_t getScreenSleepTimeout()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        load_timeout_if_needed_locked();
        const uint32_t timeout_ms = s_timeout_ms;
        xSemaphoreGive(s_mutex);
        return timeout_ms;
    }
    return kScreenTimeoutDefaultMs;
}

uint16_t readScreenTimeoutSecs()
{
    const uint32_t timeout_secs = getScreenSleepTimeout() / 1000U;
    return static_cast<uint16_t>(timeout_secs > kScreenTimeoutMaxBleSecs ? kScreenTimeoutMaxBleSecs : timeout_secs);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    const uint32_t clamped = clamp_timeout_internal(timeout_ms);
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_timeout_ms = clamped;
        s_timeout_loaded = true;
        xSemaphoreGive(s_mutex);
    }
    platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, clamped);
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();

    // Bridge the legacy hooks into the UI dispatcher so that wake/saver
    // events are serialised through the LVGL task instead of being called
    // directly from the screen-sleep FreeRTOS task.
    {
        platform::esp::idf_common::ui_dispatcher::Hooks dispatch_hooks{};
        dispatch_hooks.on_wake_from_sleep = hooks.on_wake_from_sleep;
        dispatch_hooks.show_screen_saver = hooks.show_screen_saver;
        dispatch_hooks.hide_screen_saver = hooks.hide_screen_saver;
        dispatch_hooks.show_main_menu = hooks.show_main_menu;
        platform::esp::idf_common::ui_dispatcher::init(dispatch_hooks);
        (void)platform::esp::idf_common::ui_dispatcher::ensure_drain_timer();
    }

    ensure_mutex();

    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        load_timeout_if_needed_locked();
        s_last_user_activity_ms = now_ms();
        xSemaphoreGive(s_mutex);
    }

    if (s_task == nullptr)
    {
        const BaseType_t rc = xTaskCreate(screen_sleep_task,
                                          "screen_sleep",
                                          4096,
                                          nullptr,
                                          2,
                                          &s_task);
        if (pdPASS == rc)
        {
            return;
        }
        ESP_LOGE(kTag, "Failed to start screen sleep task rc=%ld", static_cast<long>(rc));
        s_task = nullptr;
    }
}

bool isScreenSleeping()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool sleeping = s_screen_sleeping;
        xSemaphoreGive(s_mutex);
        return sleeping;
    }
    return false;
}

bool isScreenSleepDisabled()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool disabled = s_screen_sleep_disabled;
        xSemaphoreGive(s_mutex);
        return disabled;
    }
    return false;
}

bool isScreenSaverActive()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        const bool active = s_screen_saver_active;
        xSemaphoreGive(s_mutex);
        return active;
    }
    return false;
}
void wakeScreenSaver()
{
    bool should_notify_wake = false;
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        wake_screen_saver_locked();
        should_notify_wake = true;
        xSemaphoreGive(s_mutex);
    }
    if (should_notify_wake)
    {
        notify_wake();
    }
}

void enterFromScreenSaver()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        enter_ui_locked();
        xSemaphoreGive(s_mutex);
    }
    platform::esp::idf_common::ui_dispatcher::post(
        platform::esp::idf_common::ui_dispatcher::Event::HideScreenSaver);
    platform::esp::idf_common::ui_dispatcher::post(
        platform::esp::idf_common::ui_dispatcher::Event::ShowMainMenu);
}

void wakeScreenForModal()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        enter_ui_locked();
        xSemaphoreGive(s_mutex);
    }
    platform::esp::idf_common::ui_dispatcher::post(
        platform::esp::idf_common::ui_dispatcher::Event::HideScreenSaver);
}

void updateUserActivity()
{
    bool woke_from_sleep = false;
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_last_user_activity_ms = now_ms();
        if (s_screen_sleeping || s_screen_saver_active)
        {
            wake_screen_saver_locked();
            woke_from_sleep = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (woke_from_sleep)
    {
        notify_wake();
    }
}

void disableScreenSleep()
{
    bool woke = false;
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_screen_sleep_disabled = true;
        s_last_user_activity_ms = now_ms();
        if (s_screen_sleeping)
        {
            wake_screen_saver_locked();
            woke = true;
        }
        xSemaphoreGive(s_mutex);
    }
    if (woke)
    {
        notify_wake();
    }
}

void enableScreenSleep()
{
    ensure_mutex();
    if (s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_screen_sleep_disabled = false;
        s_last_user_activity_ms = now_ms();
        xSemaphoreGive(s_mutex);
    }
}

// ===================================================================
// platform::ui::screen contract — owned directly by the runtime.
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

uint32_t clamp_timeout_ms(uint32_t timeout_ms) { return clampScreenTimeoutMs(timeout_ms); }
uint32_t timeout_ms() { return getScreenSleepTimeout(); }
uint16_t timeout_secs() { return readScreenTimeoutSecs(); }
bool supports_app_timeout_setting() { return auto_sleep_supported(); }
void set_timeout_ms(uint32_t t) { setScreenSleepTimeout(t); }
void init(const Hooks& h) { initScreenSleepRuntime(adapt_hooks(h)); }
bool is_sleeping() { return isScreenSleeping(); }
bool is_sleep_disabled() { return isScreenSleepDisabled(); }
bool is_saver_active() { return isScreenSaverActive(); }
void wake_saver() { wakeScreenSaver(); }
void enter_from_saver() { enterFromScreenSaver(); }
void wake_for_modal() { wakeScreenForModal(); }
void update_user_activity() { updateUserActivity(); }
void disable_sleep() { disableScreenSleep(); }
void enable_sleep() { enableScreenSleep(); }

} // namespace platform::ui::screen
