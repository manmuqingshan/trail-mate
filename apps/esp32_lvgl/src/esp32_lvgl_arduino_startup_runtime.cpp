#include "esp32_lvgl_arduino_startup_runtime.h"

#include <Arduino.h>

#include "app/app_facade_access.h"
#include "board/BoardBase.h"
#include "display/DisplayConfig.h"
#include "esp32_lvgl_arduino_app_runtime_access.h"
#include "platform/esp/arduino_common/debug/sd_debug_log.h"
#include "platform/esp/arduino_common/display_runtime.h"
#include "platform/esp/arduino_common/startup_support.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/ui/screen_brightness_steps.h"
#include "platform/ui/settings_store.h"
#include "ui/app_registry.h"
#include "ui/app_runtime.h"
#include "ui/startup_shell.h"
#include "ui/ui_boot.h"

namespace
{

uint8_t readStartupBrightness()
{
    const int saved = platform::ui::settings_store::get_int("settings", "screen_brightness",
                                                            DEVICE_MAX_BRIGHTNESS_LEVEL);
    return platform::ui::screen_brightness_steps::clampLevel(
        saved,
        DEVICE_MAX_BRIGHTNESS_LEVEL);
}

void applyStartupBrightness(const char* stage)
{
    const auto handles = platform::esp::boards::resolveAppContextInitHandles();
    if (!handles.board)
    {
        Serial.printf("[BOOT][UI] brightness skipped stage=%s reason=no_board\n", stage ? stage : "");
        return;
    }
    const uint8_t brightness = readStartupBrightness();
    handles.board->setBrightness(brightness);
    Serial.printf("[BOOT][UI] brightness stage=%s level=%u\n",
                  stage ? stage : "",
                  static_cast<unsigned>(brightness));
}

void initializeShell()
{
    ui::startup_shell::Hooks hooks{};
    hooks.messaging = &app::messagingFacade();
    hooks.apps = ui::appCatalog();
    hooks.set_max_brightness = []()
    {
        applyStartupBrightness("shell");
    };
    hooks.show_main_menu = menu_show;
    hooks.watch_face = ui::startup_shell::defaultWatchFaceHooks();
    ui::startup_shell::initializeShell(hooks);
}

void finishStartup(bool waking_from_sleep)
{
    ui::startup_shell::finalizeStartup(waking_from_sleep);
    if (waking_from_sleep)
    {
        Serial.printf("[Setup] Updated user activity after waking from sleep\n");
        platform::esp::arduino_common::debug::append_line(
            "[Setup] Updated user activity after waking from sleep");
    }
}

} // namespace

namespace trailmate::apps::esp32_lvgl::arduino_startup_runtime
{

void run()
{
    Serial.begin(115200);
    delay(100);
    Serial.printf("\n\n[Setup] ===== SYSTEM STARTUP =====\n");
    Serial.printf("[Setup] Serial initialized at 115200 baud\n");

    platform::esp::arduino_common::startup_support::initializeClockProviders();

    const esp_sleep_wakeup_cause_t wakeup_reason =
        platform::esp::arduino_common::startup_support::detectWakeupCause();
    const bool waking_from_sleep =
        platform::esp::arduino_common::startup_support::isWakingFromSleep(wakeup_reason);
    if (waking_from_sleep)
    {
        Serial.printf("[Setup] Wakeup cause: %d\n", wakeup_reason);
    }

    platform::esp::boards::initializeBoardDisplayHardware(waking_from_sleep);
    Serial.printf("[Setup] heap=%u psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());

    Serial.println("[Setup] LVGL init begin");
    platform::esp::arduino_common::display_runtime::initialize();
    Serial.println("[Setup] LVGL init done");

    applyStartupBrightness("after_lvgl");
    ui::startup_shell::beginBootUi(waking_from_sleep, "Starting services...");
    platform::esp::boards::initializeBoardServices(waking_from_sleep);
    ui::startup_shell::setBootLogLine("Starting services...");
    ui::startup_shell::setBootLogLine("Mounting SD card...");
    const bool sd_ready = platform::esp::boards::initializeStorage();
    Serial.printf("[Setup] SD storage initialized after boot UI ready=%d\n", sd_ready ? 1 : 0);
    ui::startup_shell::setBootLogLine("Starting debug log...");

    platform::esp::arduino_common::debug::begin_sd_debug_log();
    platform::esp::arduino_common::debug::printf(
        "[Setup] board initialized wake=%d heap=%u psram=%u",
        waking_from_sleep ? 1 : 0,
        ESP.getFreeHeap(),
        ESP.getFreePsram());
    platform::esp::arduino_common::debug::append_line("[Setup] LVGL init done");
    ui::startup_shell::setBootLogLine("Checking crash dump...");
    platform::esp::arduino_common::debug::export_previous_coredump_to_sd();

    ui::startup_shell::setBootLogLine("Loading language packs...");
    ui::startup_shell::prepareBootResources();

    bool use_mock = false;
    ui::startup_shell::setBootLogLine("Initializing app context...");
    if (trailmate::apps::esp32_lvgl::arduino_app_runtime_access::initialize(use_mock))
    {
        const auto& runtime_status = trailmate::apps::esp32_lvgl::arduino_app_runtime_access::status();
        Serial.printf("[Setup] AppContext initialized handles=%d bound=%d tasks=%d\n",
                      runtime_status.board_handles_ready ? 1 : 0,
                      runtime_status.app_context_bound ? 1 : 0,
                      runtime_status.background_tasks_started ? 1 : 0);
        platform::esp::arduino_common::debug::printf(
            "[Setup] AppContext initialized handles=%d bound=%d tasks=%d",
            runtime_status.board_handles_ready ? 1 : 0,
            runtime_status.app_context_bound ? 1 : 0,
            runtime_status.background_tasks_started ? 1 : 0);
    }
    else
    {
        const auto& runtime_status = trailmate::apps::esp32_lvgl::arduino_app_runtime_access::status();
        Serial.printf("[Setup] WARNING: AppContext init failed handles=%d bound=%d tasks=%d\n",
                      runtime_status.board_handles_ready ? 1 : 0,
                      runtime_status.app_context_bound ? 1 : 0,
                      runtime_status.background_tasks_started ? 1 : 0);
        platform::esp::arduino_common::debug::printf(
            "[Setup] WARNING: AppContext init failed handles=%d bound=%d tasks=%d",
            runtime_status.board_handles_ready ? 1 : 0,
            runtime_status.app_context_bound ? 1 : 0,
            runtime_status.background_tasks_started ? 1 : 0);
    }

    ui::startup_shell::setBootLogLine("Building main menu...");
    initializeShell();
    ui::startup_shell::setBootLogLine("Startup complete");
    finishStartup(waking_from_sleep);
    // Storage recovery is armed only after the boot UI is finalized. The
    // storage runtime defers the worker itself until the first loop has had a
    // chance to present an LVGL frame.
    trailmate::apps::esp32_lvgl::arduino_app_runtime_access::startDeferredStorage();
    platform::esp::arduino_common::debug::append_line("[Setup] Startup complete");
    platform::esp::arduino_common::debug::flush();
}

} // namespace trailmate::apps::esp32_lvgl::arduino_startup_runtime
