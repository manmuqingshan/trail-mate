#include "platform/esp/arduino_common/storage/storage_runtime.h"

#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/common/memory_budget.h"

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace platform::esp::arduino_common::storage
{
namespace
{

constexpr UBaseType_t kStorageTaskStackWords = 2048;
constexpr UBaseType_t kStorageTaskPriority = 1;
constexpr size_t kStorageInternalReservation =
    static_cast<size_t>(kStorageTaskStackWords) * sizeof(StackType_t);
constexpr size_t kStorageInternalFloor = 40U * 1024U;

struct WorkerContext
{
    chat::SdStore* chat_store = nullptr;
    chat::SdProtocolPeerRepository* peer_directory = nullptr;
    chat::MeshProtocol active_protocol = chat::MeshProtocol::Meshtastic;
};

WorkerContext s_context{};
TaskHandle_t s_worker_task = nullptr;

void storage_worker(void*)
{
    const uint32_t started_ms = millis();
    Serial.printf("[Storage] worker begin active_protocol=%u\n",
                  static_cast<unsigned>(s_context.active_protocol));

    const bool chat_ready =
        s_context.chat_store == nullptr || s_context.chat_store->hydrateFromStorage();
    const bool peer_ready =
        s_context.peer_directory == nullptr ||
        s_context.peer_directory->hydrateFromStorage().succeeded();

    if (chat_ready && peer_ready)
    {
        if (s_context.chat_store)
        {
            (void)s_context.chat_store->compactDeferred();
        }
        if (s_context.peer_directory)
        {
            (void)s_context.peer_directory->compactDeferred();
        }
    }

    const unsigned long stack_free =
        static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)) *
        sizeof(StackType_t);
    Serial.printf("[Storage] worker end ready=%u elapsed_ms=%lu stack_free=%lu\n",
                  chat_ready && peer_ready ? 1U : 0U,
                  static_cast<unsigned long>(millis() - started_ms),
                  stack_free);
    s_worker_task = nullptr;
    s_context = WorkerContext{};
    vTaskDelete(nullptr);
}

} // namespace

void start_deferred_storage(chat::SdStore* chat_store,
                            chat::SdProtocolPeerRepository* peer_store,
                            chat::MeshProtocol active_protocol)
{
    if (s_worker_task)
    {
        return;
    }

    if (!chat_store && !peer_store)
    {
        Serial.printf("[Storage] deferred recovery skipped backend=ram\n");
        return;
    }

    if (!::platform::esp::common::memory::admit("storage_worker",
                                                kStorageInternalReservation,
                                                0,
                                                0,
                                                kStorageInternalFloor,
                                                0))
    {
        Serial.printf("[Storage] deferred recovery skipped reason=low_internal\n");
        return;
    }

    s_context.chat_store = chat_store;
    s_context.peer_directory = peer_store;
    s_context.active_protocol = active_protocol;
    const BaseType_t result =
        xTaskCreatePinnedToCore(&storage_worker,
                                "storage_hydrate",
                                kStorageTaskStackWords,
                                nullptr,
                                kStorageTaskPriority,
                                &s_worker_task,
                                1);
    if (result != pdPASS)
    {
        s_worker_task = nullptr;
        s_context = WorkerContext{};
        Serial.printf("[Storage] deferred recovery skipped reason=task_create_failed\n");
        return;
    }
    Serial.printf("[Storage] deferred recovery started stack_words=%u\n",
                  static_cast<unsigned>(kStorageTaskStackWords));
}

} // namespace platform::esp::arduino_common::storage
