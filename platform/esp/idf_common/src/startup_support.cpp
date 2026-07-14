#include "platform/esp/idf_common/startup_support.h"

#include <chrono>
#include <cstring>
#include <ctime>

#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/common/build_info.h"
#include "sys/clock.h"

namespace
{

uint32_t platform_millis_now()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t platform_epoch_seconds_now()
{
    const std::time_t now = std::time(nullptr);
    return now < 0 ? 0U : static_cast<uint32_t>(now);
}

void platform_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

} // namespace

namespace platform::esp::idf_common::startup_support
{

void initializeClockProviders()
{
    sys::set_millis_provider(platform_millis_now);
    sys::set_epoch_seconds_provider(platform_epoch_seconds_now);
    sys::set_sleep_provider(platform_sleep_ms);
}

void logStartupBanner(const char* tag)
{
    const esp_app_desc_t* desc = esp_app_get_description();
    const char* configured = ::platform::esp::common::build_info::firmwareVersion();
    const char* version = (configured && configured[0] != '\0' && std::strcmp(configured, "unknown") != 0)
                              ? configured
                              : (desc && desc->version[0] != '\0' ? desc->version : "unknown");
    const char* project_name = (desc && desc->project_name[0] != '\0') ? desc->project_name : "trail-mate";
    const char* app_desc_version = (desc && desc->version[0] != '\0') ? desc->version : "unknown";
    const char* build_date = (desc && desc->date[0] != '\0') ? desc->date : "unknown";
    const char* build_time = (desc && desc->time[0] != '\0') ? desc->time : "unknown";

    ESP_LOGI(tag, "startup project=%s version=%s", project_name, version);
    ESP_LOGI(tag, "idf=%s app_desc=%s compile_time=%s %s", esp_get_idf_version(), app_desc_version, build_date, build_time);
}

} // namespace platform::esp::idf_common::startup_support
