#include "esp32_lvgl_arduino_app_runtime_access.h"

#include <Arduino.h>

#include "app/app_context.h"
#include "platform/esp/arduino_common/app_runtime_bootstrap_support.h"
#include "platform/esp/arduino_common/app_runtime_support.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/ui/screen_runtime.h"

namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
{
namespace
{

Status s_status{};
bool s_was_screen_saver_active = false;
uint32_t s_wake_gate_until_ms = 0;

bool wake_gate_active(uint32_t now_ms)
{
    return s_wake_gate_until_ms != 0 &&
           static_cast<int32_t>(s_wake_gate_until_ms - now_ms) > 0;
}

} // namespace

bool initialize(bool use_mock)
{
    if (s_status.initialized)
    {
        return s_status.app_context_bound;
    }

    s_status = Status{};
    s_status.initialized = true;

    const auto handles = platform::esp::boards::resolveAppContextInitHandles();
    app::AppContext& app_context = app::AppContext::getInstance();
    platform::esp::arduino_common::AppContextBootstrapResult bootstrap_result{};
    s_status.board_handles_ready = handles.isValid();
    if (!s_status.board_handles_ready)
    {
        Serial.printf("[APP] ERROR: board runtime returned invalid AppContext handles\n");
        return false;
    }

    if (!platform::esp::arduino_common::bootstrapAppContext(app_context, handles, use_mock, &bootstrap_result))
    {
        return false;
    }

    s_status.app_context_bound = bootstrap_result.app_context_bound;

    switch (bootstrap_result.background_tasks)
    {
    case platform::esp::arduino_common::BackgroundTaskStartResult::NotSupported:
        Serial.printf("[APP] WARNING: Board type not supported for LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Failed:
        Serial.printf("[APP] WARNING: Failed to start LoRa tasks\n");
        return true;

    case platform::esp::arduino_common::BackgroundTaskStartResult::Started:
        s_status.background_tasks_started = true;
        Serial.printf("[APP] LoRa tasks started\n");
        return true;
    }

    return true;
}

void startDeferredStorage()
{
    if (!s_status.initialized || !s_status.app_context_bound)
    {
        return;
    }
    app::AppContext::getInstance().startDeferredStorage();
}

void tick()
{
    platform::esp::arduino_common::storage::tick_deferred_storage();
    const uint32_t now_ms = millis();
    const bool saver_active = platform::ui::screen::is_saver_active();
    if (saver_active)
    {
        s_was_screen_saver_active = true;
        return;
    }
    if (s_was_screen_saver_active)
    {
        s_was_screen_saver_active = false;
        s_wake_gate_until_ms = now_ms + 750U;
        return;
    }
    if (wake_gate_active(now_ms))
    {
        return;
    }

    // SdStore and the peer repository hydrate under an exclusive state lock.
    // Do not enter the foreground lifecycle while that phase is active:
    // updateCoreServices() flushes those stores and would repeatedly wait on
    // the hydration lock, starving the display loop.
    if (platform::esp::arduino_common::storage::hydration_active())
    {
        return;
    }

    platform::esp::arduino_common::tickBoundLifecycle();
}

const Status& status()
{
    return s_status;
}

} // namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
