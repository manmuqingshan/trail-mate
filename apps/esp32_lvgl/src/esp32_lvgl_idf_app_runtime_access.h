#pragma once

#include "esp32_lvgl_runtime_config.h"

namespace trailmate::apps::esp32_lvgl::idf_app_runtime_access
{

struct RuntimeStatus
{
    bool initialized = false;
    bool board_handles_ready = false;
    bool lifecycle_ready = false;
    bool app_context_bound = false;
};

bool initialize(const Esp32LvglRuntimeConfig& config);
void startDeferredStorage();
void tick();
const RuntimeStatus& status();

} // namespace trailmate::apps::esp32_lvgl::idf_app_runtime_access
