#include "platform/esp/arduino_common/storage/storage_runtime.h"

#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/common/memory_budget.h"
#include "platform/ui/screen_runtime.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

namespace platform::esp::arduino_common::storage
{
namespace
{

// ESP-IDF changes the FreeRTOS task API contract: the stack-depth argument
// and uxTaskGetStackHighWaterMark() are expressed in bytes, not words.
// Hydration crosses SdFat, the store/repository loaders, and Arduino logging;
// 2 KiB is not a viable budget for that call chain.
constexpr uint32_t kStorageTaskStackBytes = 8U * 1024U;
constexpr UBaseType_t kStorageTaskPriority = 1;
constexpr size_t kStorageInternalReservation = kStorageTaskStackBytes;
constexpr size_t kStorageInternalFloor = 40U * 1024U;
constexpr uint32_t kRetryBaseMs = 2000U;
constexpr uint32_t kRetryMaxMs = 60000U;
constexpr uint32_t kIdleStableMs = 1500U;

struct WorkerContext
{
    chat::SdStore* chat_store = nullptr;
    chat::SdProtocolPeerRepository* peer_directory = nullptr;
    chat::MeshProtocol active_protocol = chat::MeshProtocol::Meshtastic;
};

enum class WorkerMode : uint8_t
{
    Hydrate,
    Compact,
};

WorkerContext s_context{};
TaskHandle_t s_worker_task = nullptr;
WorkerMode s_worker_mode = WorkerMode::Hydrate;
uint32_t s_retry_due_ms = 0U;
uint8_t s_retry_attempt = 0U;
uint32_t s_idle_since_ms = 0U;
bool s_armed = false;
bool s_hydration_ready_event = false;
bool s_maintenance_pending = false;

void storage_worker(void*);

uint32_t retry_delay_ms()
{
    const uint8_t shift = std::min<uint8_t>(s_retry_attempt, 5U);
    return std::min<uint32_t>(kRetryBaseMs << shift, kRetryMaxMs);
}

bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return deadline_ms == 0U ||
           static_cast<int32_t>(now_ms - deadline_ms) >= 0;
}

void schedule_retry(const char* reason)
{
    ++s_retry_attempt;
    const uint32_t delay_ms = retry_delay_ms();
    s_retry_due_ms = millis() + delay_ms;
    Serial.printf("[Storage] retry scheduled reason=%s attempt=%u retry_in_ms=%lu\n",
                  reason,
                  static_cast<unsigned>(s_retry_attempt),
                  static_cast<unsigned long>(delay_ms));
}

bool start_worker(WorkerMode mode)
{
    if (s_worker_task || !s_armed)
    {
        return false;
    }
    if (!::platform::esp::common::memory::admit("storage_worker",
                                                kStorageInternalReservation,
                                                0,
                                                0,
                                                kStorageInternalFloor,
                                                0))
    {
        schedule_retry("low_internal");
        return false;
    }

    s_worker_mode = mode;
    const BaseType_t result =
        xTaskCreatePinnedToCore(&storage_worker,
                                mode == WorkerMode::Hydrate ? "storage_hydrate"
                                                            : "storage_compact",
                                kStorageTaskStackBytes,
                                nullptr,
                                kStorageTaskPriority,
                                &s_worker_task,
                                1);
    if (result != pdPASS)
    {
        s_worker_task = nullptr;
        schedule_retry("task_create_failed");
        return false;
    }
    s_retry_due_ms = 0U;
    Serial.printf("[Storage] worker started mode=%s stack_bytes=%u\n",
                  mode == WorkerMode::Hydrate ? "hydrate" : "compact",
                  static_cast<unsigned>(kStorageTaskStackBytes));
    return true;
}

void storage_worker(void*)
{
    const uint32_t started_ms = millis();
    const WorkerMode mode = s_worker_mode;
    Serial.printf("[Storage] worker begin mode=%s active_protocol=%u\n",
                  mode == WorkerMode::Hydrate ? "hydrate" : "compact",
                  static_cast<unsigned>(s_context.active_protocol));

    bool ok = true;
    if (mode == WorkerMode::Hydrate)
    {
        const bool chat_ready =
            s_context.chat_store == nullptr ||
            s_context.chat_store->hydrateFromStorage();
        const bool peer_ready =
            s_context.peer_directory == nullptr ||
            s_context.peer_directory->hydrateFromStorage().succeeded();
        ok = chat_ready && peer_ready;
        if (ok)
        {
            s_retry_attempt = 0U;
            s_hydration_ready_event = true;
            s_maintenance_pending = true;
            s_idle_since_ms = 0U;
        }
    }
    else
    {
        if (s_context.chat_store)
        {
            ok = s_context.chat_store->compactDeferred() && ok;
        }
        if (s_context.peer_directory)
        {
            ok = s_context.peer_directory->compactDeferred().succeeded() && ok;
        }
        if (ok)
        {
            s_maintenance_pending = false;
            s_retry_attempt = 0U;
        }
    }

    // ESP-IDF returns the high-water mark in bytes (unlike vanilla FreeRTOS).
    const unsigned long stack_free_bytes =
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr));
    Serial.printf("[Storage] worker end mode=%s ok=%u elapsed_ms=%lu stack_free_bytes=%lu\n",
                  mode == WorkerMode::Hydrate ? "hydrate" : "compact",
                  ok ? 1U : 0U,
                  static_cast<unsigned long>(millis() - started_ms),
                  stack_free_bytes);
    s_worker_task = nullptr;
    if (!ok)
    {
        schedule_retry(mode == WorkerMode::Hydrate ? "hydrate_failed"
                                                   : "compact_failed");
    }
    vTaskDelete(nullptr);
}

} // namespace

void start_deferred_storage(chat::SdStore* chat_store,
                            chat::SdProtocolPeerRepository* peer_store,
                            chat::MeshProtocol active_protocol)
{
    if (s_armed)
    {
        return;
    }

    if (!chat_store && !peer_store)
    {
        Serial.printf("[Storage] deferred recovery skipped backend=ram\n");
        return;
    }

    s_context.chat_store = chat_store;
    s_context.peer_directory = peer_store;
    s_context.active_protocol = active_protocol;
    s_armed = true;
    s_retry_attempt = 0U;
    s_retry_due_ms = 0U;
    (void)start_worker(WorkerMode::Hydrate);
}

void tick_deferred_storage()
{
    if (!s_armed || s_worker_task)
    {
        return;
    }

    const uint32_t now_ms = millis();
    if (s_retry_due_ms != 0U && !deadline_reached(now_ms, s_retry_due_ms))
    {
        return;
    }

    if (s_maintenance_pending)
    {
        if (!::platform::ui::screen::is_sleeping() ||
            ::platform::ui::screen::is_saver_active())
        {
            s_idle_since_ms = 0U;
            return;
        }
        if (s_idle_since_ms == 0U)
        {
            s_idle_since_ms = now_ms;
            return;
        }
        if (now_ms - s_idle_since_ms < kIdleStableMs)
        {
            return;
        }
        (void)start_worker(WorkerMode::Compact);
        return;
    }

    (void)start_worker(WorkerMode::Hydrate);
}

bool consume_hydration_ready()
{
    if (!s_hydration_ready_event)
    {
        return false;
    }
    s_hydration_ready_event = false;
    return true;
}

} // namespace platform::esp::arduino_common::storage
