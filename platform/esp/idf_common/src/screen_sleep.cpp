/**
 * @file screen_sleep.cpp
 * @brief ESP-IDF adapter for the shared screen-power state machine.
 */

#include "screen_sleep.h"

#include <cstdint>

#include "board/BoardBase.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/ui_dispatcher.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/screen_power_state_machine.h"
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

using platform::ui::screen_power::Effects;
using platform::ui::screen_power::Event;
using platform::ui::screen_power::Snapshot;
using platform::ui::screen_power::State;
using platform::ui::screen_power::StateMachine;

constexpr const char* kTag = "idf-screen-power";
constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
constexpr std::uint32_t kQueueDepth = 32;
constexpr std::uint32_t kTaskPeriodMs = 100;
constexpr std::uint8_t kIdfMaxBrightnessLevel = 16U;

ScreenSleepHooks s_hooks{};
StateMachine s_machine{};
SemaphoreHandle_t s_state_mutex = nullptr;
QueueHandle_t s_event_queue = nullptr;
TaskHandle_t s_task = nullptr;
std::uint8_t s_saved_screen_brightness = kIdfMaxBrightnessLevel;

std::uint32_t now_ms()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

void ensure_state_mutex()
{
    if (s_state_mutex == nullptr)
    {
        s_state_mutex = xSemaphoreCreateMutex();
    }
}

void ensure_event_queue()
{
    if (s_event_queue == nullptr)
    {
        s_event_queue = xQueueCreate(kQueueDepth, sizeof(Event));
    }
}

void post_ui_event(platform::esp::idf_common::ui_dispatcher::Event event)
{
    (void)platform::esp::idf_common::ui_dispatcher::post(event);
}

void apply_effects(const Effects& effects)
{
    if (effects.sleep_display)
    {
        s_saved_screen_brightness = platform::ui::device::screen_brightness();
        (void)platform::esp::idf_common::bsp_runtime::sleep_display();
    }

    if (effects.wake_display)
    {
        (void)platform::esp::idf_common::bsp_runtime::wake_display();
        platform::ui::device::set_screen_brightness(s_saved_screen_brightness);
    }

    if (effects.hide_saver)
    {
        post_ui_event(platform::esp::idf_common::ui_dispatcher::Event::HideScreenSaver);
    }

    if (effects.show_saver)
    {
        post_ui_event(platform::esp::idf_common::ui_dispatcher::Event::ShowScreenSaver);
    }

    if (effects.show_main_menu)
    {
        post_ui_event(platform::esp::idf_common::ui_dispatcher::Event::ShowMainMenu);
    }
}

void dispatch_event(Event event)
{
    Effects effects{};
    ensure_state_mutex();
    if (s_state_mutex == nullptr ||
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    effects = s_machine.dispatch(event, now_ms());
    xSemaphoreGive(s_state_mutex);
    apply_effects(effects);
}

bool touch_wake_pending()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return platform::ui::screen::is_sleeping() &&
           trail_mate_tab5_touch_interrupt_active();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return platform::ui::screen::is_sleeping() &&
           boards::t_display_p4::runtime_support::touch_interrupt_active();
#else
    return false;
#endif
}

void screen_power_task(void*)
{
    while (true)
    {
        Event event = Event::Tick;
        if (s_event_queue != nullptr)
        {
            while (xQueueReceive(s_event_queue, &event, 0) == pdTRUE)
            {
                dispatch_event(event);
            }
        }
        if (touch_wake_pending())
        {
            dispatch_event(Event::Input);
        }
        dispatch_event(Event::Tick);
        vTaskDelay(pdMS_TO_TICKS(kTaskPeriodMs));
    }
}

bool post_event(Event event)
{
    ensure_event_queue();
    if (s_event_queue == nullptr)
    {
        return false;
    }
    return xQueueSend(s_event_queue, &event, 0) == pdTRUE;
}

Snapshot snapshot()
{
    Snapshot value{};
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        value = s_machine.snapshot();
        xSemaphoreGive(s_state_mutex);
    }
    return value;
}

void initialize_state()
{
    ensure_state_mutex();
    ensure_event_queue();
    if (s_state_mutex == nullptr)
    {
        return;
    }
    if (xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        const std::uint32_t persisted_timeout =
            platform::ui::settings_store::get_uint(
                kSettingsNs,
                kScreenTimeoutKey,
                StateMachine::kDefaultTimeoutMs);
        s_machine.set_timeout_ms(persisted_timeout);
        (void)s_machine.dispatch(Event::Initialize, now_ms());
        xSemaphoreGive(s_state_mutex);
    }
}

} // namespace

uint32_t clampScreenTimeoutMs(uint32_t timeout_ms)
{
    return StateMachine::clamp_timeout_ms(timeout_ms);
}

uint32_t getScreenSleepTimeout()
{
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        const std::uint32_t timeout_ms = s_machine.timeout_ms();
        xSemaphoreGive(s_state_mutex);
        return timeout_ms;
    }
    return StateMachine::kDefaultTimeoutMs;
}

uint16_t readScreenTimeoutSecs()
{
    const std::uint32_t seconds = getScreenSleepTimeout() / 1000U;
    return static_cast<uint16_t>(seconds > 900U ? 900U : seconds);
}

void setScreenSleepTimeout(uint32_t timeout_ms)
{
    const std::uint32_t clamped = clampScreenTimeoutMs(timeout_ms);
    platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, clamped);
    ensure_state_mutex();
    if (s_state_mutex != nullptr &&
        xSemaphoreTake(s_state_mutex, portMAX_DELAY) == pdTRUE)
    {
        s_machine.set_timeout_ms(clamped);
        xSemaphoreGive(s_state_mutex);
    }
}

void initScreenSleepRuntime(const ScreenSleepHooks& hooks)
{
    s_hooks = hooks;
    platform::esp::idf_common::bsp_runtime::ensure_nvs_ready();

    platform::esp::idf_common::ui_dispatcher::Hooks dispatcher_hooks{};
    dispatcher_hooks.on_wake_from_sleep = hooks.on_wake_from_sleep;
    dispatcher_hooks.show_screen_saver = hooks.show_screen_saver;
    dispatcher_hooks.hide_screen_saver = hooks.hide_screen_saver;
    dispatcher_hooks.show_main_menu = hooks.show_main_menu;
    platform::esp::idf_common::ui_dispatcher::init(dispatcher_hooks);
    (void)platform::esp::idf_common::ui_dispatcher::ensure_drain_timer();

    initialize_state();
    if (s_task == nullptr)
    {
        const BaseType_t result = xTaskCreate(
            screen_power_task,
            "screen_power",
            4096,
            nullptr,
            2,
            &s_task);
        if (result != pdPASS)
        {
            ESP_LOGE(kTag, "Failed to start screen power task rc=%ld",
                     static_cast<long>(result));
            s_task = nullptr;
        }
    }
}

bool isScreenSleeping()
{
    return snapshot().state == State::Sleeping;
}

bool isScreenSaverActive()
{
    return snapshot().state == State::WakePreview;
}

namespace platform::ui::screen
{

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
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return false;
#else
    return true;
#endif
}

void set_timeout_ms(uint32_t timeout_ms)
{
    setScreenSleepTimeout(timeout_ms);
}

void init(const Hooks& hooks)
{
    ScreenSleepHooks adapted{};
    adapted.format_time = hooks.format_time;
    adapted.read_unread_count = hooks.read_unread_count;
    adapted.show_main_menu = hooks.show_main_menu;
    adapted.on_wake_from_sleep = hooks.on_wake_from_sleep;
    adapted.show_screen_saver = hooks.show_screen_saver;
    adapted.hide_screen_saver = hooks.hide_screen_saver;
    adapted.present_screen_saver = hooks.present_screen_saver;
    initScreenSleepRuntime(adapted);
}

bool is_sleeping()
{
    return isScreenSleeping();
}

bool is_sleep_disabled()
{
    return snapshot().sleep_disable_depth != 0;
}

bool is_saver_active()
{
    return isScreenSaverActive();
}

void handle_input()
{
    (void)post_event(Event::Input);
}

void handle_input_release()
{
    (void)post_event(Event::InputRelease);
}

void wake_for_modal()
{
    (void)post_event(Event::ModalWake);
}

void record_activity()
{
    (void)post_event(Event::Activity);
}

void disable_sleep()
{
    (void)post_event(Event::DisableSleep);
}

void enable_sleep()
{
    (void)post_event(Event::EnableSleep);
}

} // namespace platform::ui::screen
