#include "platform/esp/idf_common/storage_runtime.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/ui/screen_runtime.h"

#include <algorithm>
#include <cstdint>

namespace platform::esp::idf_common::storage
{
namespace
{

constexpr const char* kTag = "idf-storage";
constexpr UBaseType_t kStackWords = 2048;
constexpr std::size_t kInternalReservation =
    static_cast<std::size_t>(kStackWords) * sizeof(StackType_t);
constexpr std::size_t kInternalFloor = 40U * 1024U;
constexpr uint32_t kRetryBaseMs = 2000U;
constexpr uint32_t kRetryMaxMs = 60000U;
constexpr uint32_t kIdleStableMs = 1500U;

enum class Mode : uint8_t
{
    Hydrate,
    Compact,
};

chat::SdStore* s_store = nullptr;
TaskHandle_t s_task = nullptr;
Mode s_mode = Mode::Hydrate;
uint32_t s_retry_due_ms = 0U;
uint8_t s_retry_attempt = 0U;
uint32_t s_idle_since_ms = 0U;
bool s_armed = false;
bool s_ready_event = false;
bool s_maintenance_pending = false;

void worker(void*);

uint32_t now_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

uint32_t retry_delay_ms()
{
    const uint8_t shift = std::min<uint8_t>(s_retry_attempt, 5U);
    return std::min<uint32_t>(kRetryBaseMs << shift, kRetryMaxMs);
}

bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return deadline == 0U || static_cast<int32_t>(now - deadline) >= 0;
}

void schedule_retry(const char* reason)
{
    ++s_retry_attempt;
    const uint32_t delay = retry_delay_ms();
    s_retry_due_ms = now_ms() + delay;
    ESP_LOGW(kTag,
             "retry scheduled reason=%s attempt=%u retry_in_ms=%lu",
             reason,
             static_cast<unsigned>(s_retry_attempt),
             static_cast<unsigned long>(delay));
}

bool start_worker(Mode mode)
{
    if (!s_armed || s_task)
    {
        return false;
    }
    if (!platform::esp::common::memory::admit("idf_storage_worker",
                                              kInternalReservation,
                                              0,
                                              0,
                                              kInternalFloor,
                                              0))
    {
        schedule_retry("low_internal");
        return false;
    }
    s_mode = mode;
    if (xTaskCreatePinnedToCore(&worker,
                                mode == Mode::Hydrate ? "idf_store_hydrate"
                                                      : "idf_store_compact",
                                kStackWords,
                                nullptr,
                                1,
                                &s_task,
                                1) != pdPASS)
    {
        s_task = nullptr;
        schedule_retry("task_create_failed");
        return false;
    }
    s_retry_due_ms = 0U;
    ESP_LOGI(kTag, "worker started mode=%s", mode == Mode::Hydrate ? "hydrate" : "compact");
    return true;
}

void worker(void*)
{
    const Mode mode = s_mode;
    bool ok = mode == Mode::Hydrate ? s_store->hydrateFromStorage()
                                    : s_store->compactDeferred();
    if (ok && mode == Mode::Hydrate)
    {
        s_ready_event = true;
        s_maintenance_pending = true;
        s_retry_attempt = 0U;
        s_idle_since_ms = 0U;
    }
    else if (ok)
    {
        s_maintenance_pending = false;
        s_retry_attempt = 0U;
    }
    if (!ok)
    {
        schedule_retry(mode == Mode::Hydrate ? "hydrate_failed"
                                             : "compact_failed");
    }
    ESP_LOGI(kTag,
             "worker finished mode=%s ok=%d stack_free=%u",
             mode == Mode::Hydrate ? "hydrate" : "compact",
             ok ? 1 : 0,
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    s_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void start_deferred_storage(chat::SdStore* store)
{
    if (!store || s_armed)
    {
        return;
    }
    s_store = store;
    s_armed = true;
    (void)start_worker(Mode::Hydrate);
}

void tick_deferred_storage()
{
    if (!s_armed || s_task)
    {
        return;
    }
    const uint32_t now = now_ms();
    if (!deadline_reached(now, s_retry_due_ms))
    {
        return;
    }
    if (s_maintenance_pending)
    {
        if (!platform::ui::screen::is_sleeping() ||
            platform::ui::screen::is_saver_active())
        {
            s_idle_since_ms = 0U;
            return;
        }
        if (s_idle_since_ms == 0U)
        {
            s_idle_since_ms = now;
            return;
        }
        if (now - s_idle_since_ms < kIdleStableMs)
        {
            return;
        }
        (void)start_worker(Mode::Compact);
        return;
    }
    (void)start_worker(Mode::Hydrate);
}

bool consume_hydration_ready()
{
    if (!s_ready_event)
    {
        return false;
    }
    s_ready_event = false;
    return true;
}

} // namespace platform::esp::idf_common::storage
