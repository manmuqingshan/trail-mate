#pragma once

#include "esp32_lvgl_runtime_config.h"
#include "platform/esp/boards/board_runtime.h"

namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
{

bool initialize(const platform::esp::boards::AppContextInitHandles& handles,
                const Esp32LvglRuntimeConfig& config);
bool isInitialized();

} // namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
