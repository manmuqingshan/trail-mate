#pragma once

#include <cstdint>

namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
{

struct Status
{
    bool initialized = false;
    bool board_handles_ready = false;
    bool app_context_bound = false;
    bool background_tasks_started = false;
};

enum class ConfigurationPreloadResult : uint8_t
{
    Ready,
    RepairRequired,
    Failed,
};

ConfigurationPreloadResult preloadConfiguration();
bool initialize(bool use_mock);
void startDeferredStorage();
void tick();
const Status& status();

} // namespace trailmate::apps::esp32_lvgl::arduino_app_runtime_access
