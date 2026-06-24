#include "esp32_lvgl_idf_app_runtime_access.h"

#if defined(ESP_PLATFORM)
#include "app/app_facade_access.h"
#include "esp32_lvgl_idf_app_facade_runtime.h"
#include "esp_log.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/app_runtime_support.h"
#endif

namespace trailmate::apps::esp32_lvgl::idf_app_runtime_access
{
namespace
{

RuntimeStatus s_status{};

} // namespace

bool initialize(const Esp32LvglRuntimeConfig& config)
{
    if (s_status.initialized)
    {
        return s_status.app_context_bound;
    }

    s_status.initialized = true;

#if defined(ESP_PLATFORM)
    platform::esp::boards::AppContextInitHandles handles{};
    s_status.board_handles_ready = platform::esp::boards::tryResolveAppContextInitHandles(&handles) &&
                                   handles.isValid();
    s_status.app_context_bound =
        s_status.board_handles_ready && idf_app_facade_runtime::initialize(handles, config);
    s_status.lifecycle_ready = s_status.app_context_bound;

    if (!s_status.board_handles_ready)
    {
        ESP_LOGW(config.log_tag, "IDF app runtime board handles unavailable for %s", config.target_name);
    }
    if (!s_status.app_context_bound)
    {
        ESP_LOGW(config.log_tag,
                 "IDF app facade runtime is not bound for %s; app loop will run platform/UI idle ticks only",
                 config.target_name);
    }
#else
    (void)config;
#endif

    return s_status.app_context_bound;
}

void tick()
{
#if defined(ESP_PLATFORM)
    if (app::hasAppFacade())
    {
        s_status.app_context_bound = true;
        s_status.lifecycle_ready = true;
        platform::esp::idf_common::tickBoundLifecycle();
    }
#endif
}

const RuntimeStatus& status()
{
    return s_status;
}

} // namespace trailmate::apps::esp32_lvgl::idf_app_runtime_access
