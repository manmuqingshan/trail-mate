/**
 * @file app_tasks.h
 * @brief Application task management
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/ports/i_mesh_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdint.h>

namespace app
{

/**
 * @brief Optional bounded sideband parser for application-owned raw RF frames.
 *
 * The interceptor runs in the mesh task before the configured mesh adapter.
 * It must return false for every frame it does not own, and it must not block,
 * pause radio tasks, or perform radio I/O from that task.  Returning true
 * consumes the frame permanently; this is how an application protocol such as
 * VMP stays outside the MT/MC/RT decoder and forwarding paths.
 */
class IRawRadioPacketInterceptor
{
  public:
    virtual ~IRawRadioPacketInterceptor() = default;
    virtual bool tryConsume(const uint8_t* data,
                            size_t size,
                            float rssi,
                            float snr) = 0;
};

/**
 * @brief Task management
 */
class AppTasks
{
  public:
    // Keep always-on radio queues shallow so network stacks retain internal RAM.
    static constexpr size_t RADIO_QUEUE_SIZE = 6;
    static constexpr size_t MESH_QUEUE_SIZE = 6;

    struct RadioPacket
    {
        uint8_t* data = nullptr;
        size_t size = 0;
        bool is_tx = false; // true for TX, false for RX
        float rssi = 0.0f;
        float snr = 0.0f;
        uint32_t queued_ms = 0;
        uint32_t next_attempt_ms = 0;
        uint8_t retry_count = 0;
    };

    /**
     * @brief Initialize tasks
     * @param board Board instance
     * @param adapter Mesh adapter
     */
    static bool init(LoraBoard& board, chat::IMeshAdapter* adapter);

    /**
     * @brief Radio task (high priority)
     */
    static void radioTask(void* pvParameters);

    /**
     * @brief Mesh task (medium priority)
     */
    static void meshTask(void* pvParameters);

    /**
     * @brief Get radio TX queue
     */
    static QueueHandle_t getRadioTxQueue()
    {
        return radio_tx_queue_;
    }

    /**
     * @brief Get radio RX queue
     */
    static QueueHandle_t getRadioRxQueue()
    {
        return radio_rx_queue_;
    }

    static TaskHandle_t getRadioTaskHandle()
    {
        return radio_task_handle_;
    }

    static TaskHandle_t getMeshTaskHandle()
    {
        return mesh_task_handle_;
    }

    /**
     * @brief Pause radio + mesh tasks (for exclusive radio modes like walkie-talkie)
     */
    static bool pauseRadioTasks(uint32_t timeout_ms = 2000U);

    /**
     * @brief Resume radio + mesh tasks after pause
     */
    static void resumeRadioTasks();

    /**
     * @brief Check if radio tasks are paused
     */
    static bool areRadioTasksPaused()
    {
        return radio_tasks_paused_;
    }

    /**
     * @brief Record whether the shared radio receive path is currently armed.
     */
    static void setRadioReceiveActive(bool active);

    /**
     * @brief Ask the radio task to re-arm RX on its next poll cycle.
     */
    static void requestRadioReceiveRestart();
    /**
     * @brief Gate the shared LoRa RX path while keeping queued TX possible.
     */
    static void setRadioReceiveSuppressed(bool suppressed);
    static bool isRadioReceiveSuppressed();
    static void setRadioTransmitActive(bool active);
    static bool isRadioTransmitActive();
    static bool enqueueRadioTransmit(const uint8_t* data, size_t size);

    /**
     * @brief Installs/removes the one application-owned raw packet interceptor.
     *
     * The caller retains ownership and must clear the pointer before destroying
     * the interceptor.  Null leaves existing mesh-adapter behavior unchanged.
     */
    static void setRawRadioPacketInterceptor(IRawRadioPacketInterceptor* interceptor);

    class ScopedRadioTransmitActivity
    {
      public:
        ScopedRadioTransmitActivity();
        ~ScopedRadioTransmitActivity();
        ScopedRadioTransmitActivity(const ScopedRadioTransmitActivity&) = delete;
        ScopedRadioTransmitActivity& operator=(const ScopedRadioTransmitActivity&) = delete;
    };

  private:
    static QueueHandle_t radio_tx_queue_;
    static QueueHandle_t radio_rx_queue_;
    static QueueHandle_t mesh_queue_;
    static TaskHandle_t radio_task_handle_;
    static TaskHandle_t mesh_task_handle_;
    static LoraBoard* board_;
    static chat::IMeshAdapter* adapter_;
    static IRawRadioPacketInterceptor* raw_radio_packet_interceptor_;
    static uint8_t* radio_rx_scratch_;
    static volatile bool radio_tasks_paused_;
    static volatile bool radio_receive_active_;
    static volatile bool radio_receive_restart_pending_;
    static volatile bool radio_receive_suppressed_;
    static volatile bool radio_transmit_active_;
    static volatile bool radio_task_quiesced_;
    static volatile bool mesh_task_quiesced_;
};

} // namespace app
