/**
 * @file app_tasks.cpp
 * @brief Application task implementation
 */

#include "platform/esp/arduino_common/app_tasks.h"

#include "platform/esp/common/shared_spi_lock.h"
#include "platform/ui/reticulum_call_runtime.h"
#include <Arduino.h>
#include <RadioLib.h>
#include <cstring>
#include <esp_heap_caps.h>

#ifndef LORA_LOG_ENABLE
#define LORA_LOG_ENABLE 0
#endif

#if LORA_LOG_ENABLE
#define LORA_LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LORA_LOG(...) \
    do                \
    {                 \
    } while (0)
#endif

namespace app
{

namespace
{
constexpr TickType_t kRadioPollDelay = pdMS_TO_TICKS(10);
constexpr TickType_t kRadioDisplayPressurePollDelay = pdMS_TO_TICKS(50);
constexpr uint32_t kRadioDisplayPressureWindowMs = 300;
constexpr uint32_t kRadioTaskStackBytes = 3 * 1024;
constexpr uint32_t kMeshTaskStackBytes = 6 * 1024;
constexpr uint32_t kRadioRxSummaryIntervalMs = 5000;

struct RadioRxSummary
{
    uint32_t packets = 0;
    uint32_t bytes = 0;
    uint32_t queue_drops = 0;
    uint32_t alloc_drops = 0;
    uint32_t read_failures = 0;
    uint32_t other_irqs = 0;
    uint32_t last_log_ms = 0;
};

bool display_spi_pressure_for_radio()
{
    return ::platform::esp::common::display_spi_recently_timed_out(
        millis(),
        kRadioDisplayPressureWindowMs);
}

uint32_t radio_rx_done_mask()
{
    uint32_t mask = 0;
#if defined(RADIOLIB_SX126X_IRQ_RX_DONE)
    mask |= RADIOLIB_SX126X_IRQ_RX_DONE;
#endif
#if defined(RADIOLIB_SX128X_IRQ_RX_DONE)
    mask |= RADIOLIB_SX128X_IRQ_RX_DONE;
#endif
#if defined(ARDUINO_LILYGO_LORA_LR1121) && defined(RADIOLIB_LR11X0_IRQ_RX_DONE)
    mask |= RADIOLIB_LR11X0_IRQ_RX_DONE;
#endif
    return mask;
}

uint32_t radio_terminal_irq_mask()
{
    uint32_t mask = radio_rx_done_mask();
#if defined(RADIOLIB_SX126X_IRQ_CRC_ERR)
    mask |= RADIOLIB_SX126X_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_HEADER_ERR)
    mask |= RADIOLIB_SX126X_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_TIMEOUT)
    mask |= RADIOLIB_SX126X_IRQ_TIMEOUT;
#endif
#if defined(RADIOLIB_SX128X_IRQ_CRC_ERR)
    mask |= RADIOLIB_SX128X_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_SX128X_IRQ_HEADER_ERR)
    mask |= RADIOLIB_SX128X_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_SX128X_IRQ_TIMEOUT)
    mask |= RADIOLIB_SX128X_IRQ_TIMEOUT;
#endif
#if defined(ARDUINO_LILYGO_LORA_LR1121)
#if defined(RADIOLIB_LR11X0_IRQ_CRC_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_HEADER_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_TIMEOUT)
    mask |= RADIOLIB_LR11X0_IRQ_TIMEOUT;
#endif
#endif
    return mask;
}

bool radio_read_state_has_payload(int state)
{
    if (state == RADIOLIB_ERR_NONE)
    {
        return true;
    }
#if defined(RADIOLIB_ERR_CRC_MISMATCH)
    // RadioLib drivers can report CRC mismatch after the FIFO payload has
    // already been copied. Let protocol-level parsers/signatures decide
    // whether the bytes are usable instead of dropping them before the adapter.
    return state == RADIOLIB_ERR_CRC_MISMATCH;
#else
    return false;
#endif
}

void maybe_log_radio_rx_summary(RadioRxSummary& summary)
{
#if LORA_LOG_ENABLE
    const uint32_t now_ms = millis();
    if (summary.last_log_ms == 0)
    {
        summary.last_log_ms = now_ms;
    }
    if ((now_ms - summary.last_log_ms) < kRadioRxSummaryIntervalMs)
    {
        return;
    }
    if (summary.packets != 0 || summary.queue_drops != 0 || summary.alloc_drops != 0 ||
        summary.read_failures != 0 || summary.other_irqs != 0)
    {
        Serial.printf("[LORA] RX stats packets=%lu bytes=%lu queue_drop=%lu alloc_drop=%lu read_fail=%lu other_irq=%lu\n",
                      static_cast<unsigned long>(summary.packets),
                      static_cast<unsigned long>(summary.bytes),
                      static_cast<unsigned long>(summary.queue_drops),
                      static_cast<unsigned long>(summary.alloc_drops),
                      static_cast<unsigned long>(summary.read_failures),
                      static_cast<unsigned long>(summary.other_irqs));
    }
    summary = RadioRxSummary{};
    summary.last_log_ms = now_ms;
#else
    (void)summary;
#endif
}
} // namespace

// Static members
QueueHandle_t AppTasks::radio_tx_queue_ = nullptr;
QueueHandle_t AppTasks::radio_rx_queue_ = nullptr;
QueueHandle_t AppTasks::mesh_queue_ = nullptr;
TaskHandle_t AppTasks::radio_task_handle_ = nullptr;
TaskHandle_t AppTasks::mesh_task_handle_ = nullptr;
LoraBoard* AppTasks::board_ = nullptr;
chat::IMeshAdapter* AppTasks::adapter_ = nullptr;
bool AppTasks::radio_tasks_paused_ = false;
volatile bool AppTasks::radio_receive_active_ = false;
volatile bool AppTasks::radio_receive_restart_pending_ = true;
volatile bool AppTasks::radio_receive_suppressed_ = false;
volatile bool AppTasks::radio_transmit_active_ = false;

bool AppTasks::init(LoraBoard& board, chat::IMeshAdapter* adapter)
{
    board_ = &board;
    adapter_ = adapter;
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = !radio_receive_suppressed_;

    // Create queues
    radio_tx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    radio_rx_queue_ = xQueueCreate(RADIO_QUEUE_SIZE, sizeof(RadioPacket));
    mesh_queue_ = xQueueCreate(MESH_QUEUE_SIZE, sizeof(RadioPacket));

    if (!radio_tx_queue_ || !radio_rx_queue_ || !mesh_queue_)
    {
        return false;
    }

    // Create radio task (high priority)
    BaseType_t result = xTaskCreate(
        radioTask,
        "radio_task",
        kRadioTaskStackBytes,
        nullptr,
        10, // High priority
        &radio_task_handle_);

    if (result != pdPASS)
    {
        return false;
    }

    // Create mesh task (medium priority)
    result = xTaskCreate(
        meshTask,
        "mesh_task",
        kMeshTaskStackBytes,
        nullptr,
        5, // Medium priority
        &mesh_task_handle_);

    return (result == pdPASS);
}

void AppTasks::pauseRadioTasks()
{
    if (radio_tasks_paused_)
    {
        return;
    }
    radio_tasks_paused_ = true;
    requestRadioReceiveRestart();

    if (radio_task_handle_)
    {
        vTaskSuspend(radio_task_handle_);
    }
    if (mesh_task_handle_)
    {
        vTaskSuspend(mesh_task_handle_);
    }

    if (radio_tx_queue_)
    {
        xQueueReset(radio_tx_queue_);
    }
    if (radio_rx_queue_)
    {
        xQueueReset(radio_rx_queue_);
    }
    if (mesh_queue_)
    {
        xQueueReset(mesh_queue_);
    }
}

void AppTasks::resumeRadioTasks()
{
    if (!radio_tasks_paused_)
    {
        return;
    }
    radio_tasks_paused_ = false;

    if (radio_task_handle_)
    {
        vTaskResume(radio_task_handle_);
    }
    if (mesh_task_handle_)
    {
        vTaskResume(mesh_task_handle_);
    }
    requestRadioReceiveRestart();
}

void AppTasks::setRadioReceiveActive(bool active)
{
    if (radio_receive_suppressed_ && active)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
        return;
    }
    radio_receive_active_ = active;
    radio_receive_restart_pending_ = !active;
}

void AppTasks::requestRadioReceiveRestart()
{
    if (radio_receive_suppressed_)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
        return;
    }
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = true;
}

void AppTasks::setRadioReceiveSuppressed(bool suppressed)
{
    if (radio_receive_suppressed_ == suppressed)
    {
        return;
    }
    radio_receive_suppressed_ = suppressed;
    radio_receive_active_ = false;
    radio_receive_restart_pending_ = !suppressed;
    Serial.printf("[LORA] RX gate %s\n", suppressed ? "suppressed" : "enabled");
}

bool AppTasks::isRadioReceiveSuppressed()
{
    return radio_receive_suppressed_;
}

void AppTasks::setRadioTransmitActive(bool active)
{
    radio_transmit_active_ = active;
    if (active)
    {
        radio_receive_active_ = false;
        radio_receive_restart_pending_ = false;
    }
    else
    {
        radio_receive_restart_pending_ = !radio_receive_suppressed_;
    }
}

bool AppTasks::isRadioTransmitActive()
{
    return radio_transmit_active_;
}

bool AppTasks::enqueueRadioTransmit(const uint8_t* data, size_t size)
{
    if (!data || size == 0 || size > 255 || !radio_tx_queue_)
    {
        return false;
    }

    uint8_t* copy = static_cast<uint8_t*>(
        heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!copy)
    {
        return false;
    }
    std::memcpy(copy, data, size);

    RadioPacket packet{};
    packet.data = copy;
    packet.size = size;
    packet.is_tx = true;
    if (xQueueSend(radio_tx_queue_, &packet, 0) != pdPASS)
    {
        heap_caps_free(copy);
        LORA_LOG("[LORA] TX queue full len=%u\n", static_cast<unsigned>(size));
        return false;
    }

    requestRadioReceiveRestart();
    return true;
}

AppTasks::ScopedRadioTransmitActivity::ScopedRadioTransmitActivity()
{
    AppTasks::setRadioTransmitActive(true);
}

AppTasks::ScopedRadioTransmitActivity::~ScopedRadioTransmitActivity()
{
    AppTasks::setRadioTransmitActive(false);
}

void AppTasks::radioTask(void* pvParameters)
{
    (void)pvParameters;

    uint8_t rx_buffer[255];
    const uint32_t rx_done_mask = radio_rx_done_mask();
    const uint32_t terminal_irq_mask = radio_terminal_irq_mask();
    RadioRxSummary rx_summary{};

    while (true)
    {
        if (::platform::ui::reticulum_call::realtime_mode_active())
        {
            requestRadioReceiveRestart();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        bool should_restart_rx = radio_receive_restart_pending_;
        bool handled_tx = false;

        if (radio_transmit_active_)
        {
            vTaskDelay(kRadioDisplayPressurePollDelay);
            continue;
        }

        // Process TX queue
        RadioPacket tx_packet;
        if (xQueueReceive(radio_tx_queue_, &tx_packet, 0) == pdPASS)
        {
            handled_tx = tx_packet.is_tx && tx_packet.data && tx_packet.size > 0;
            if (tx_packet.is_tx && tx_packet.data && tx_packet.size > 0)
            {
                // Send packet
                if (board_ && board_->isRadioOnline())
                {
                    requestRadioReceiveRestart();
                    int state = RADIOLIB_ERR_NONE;
                    setRadioTransmitActive(true);
                    state = board_->transmitRadio(tx_packet.data, tx_packet.size);
                    setRadioTransmitActive(false);
                    LORA_LOG("[LORA] TX queue len=%u state=%d\n", (unsigned)tx_packet.size, state);
                    if (state == RADIOLIB_ERR_NONE && !radio_receive_suppressed_)
                    {
                        int rx_state = board_->startRadioReceive();
                        if (rx_state == RADIOLIB_ERR_NONE)
                        {
                            setRadioReceiveActive(true);
                            should_restart_rx = false;
                        }
                        else
                        {
                            requestRadioReceiveRestart();
                            LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                        }
                    }
                    else if (state == RADIOLIB_ERR_NONE)
                    {
                        radio_receive_active_ = false;
                        radio_receive_restart_pending_ = false;
                    }
                    else
                    {
                        requestRadioReceiveRestart();
                    }
                }
                else
                {
                    LORA_LOG("[LORA] TX drop (radio offline) len=%u\n", (unsigned)tx_packet.size);
                }
                free(tx_packet.data);
            }
        }

        if (radio_receive_suppressed_)
        {
            radio_receive_active_ = false;
            radio_receive_restart_pending_ = false;
            maybe_log_radio_rx_summary(rx_summary);
            vTaskDelay(kRadioDisplayPressurePollDelay);
            continue;
        }

        // Poll for RX (non-blocking)
        if (board_ && board_->isRadioOnline())
        {
            if (!radio_receive_active_ || radio_receive_restart_pending_ || should_restart_rx)
            {
                int rx_state = board_->startRadioReceive();
                if (rx_state == RADIOLIB_ERR_NONE)
                {
                    setRadioReceiveActive(true);
                    should_restart_rx = false;
                }
                else
                {
                    requestRadioReceiveRestart();
                    LORA_LOG("[LORA] RX start fail state=%d\n", rx_state);
                }
            }
            const bool display_pressure = display_spi_pressure_for_radio();
            if (display_pressure &&
                !handled_tx &&
                radio_receive_active_ &&
                !radio_receive_restart_pending_ &&
                !should_restart_rx)
            {
                vTaskDelay(kRadioDisplayPressurePollDelay);
                continue;
            }
            // Check if data available using RadioLib IRQs
            int packet_length = 0;
            uint32_t irq = board_->getRadioIrqFlags();
            if ((irq & rx_done_mask) != 0)
            {
                packet_length = static_cast<int>(board_->getRadioPacketLength(true));
                if (packet_length > 0 && packet_length <= 255)
                {
                    int state = board_->readRadioData(rx_buffer, packet_length);
                    if (radio_read_state_has_payload(state))
                    {
                        RadioPacket rx_packet;
                        rx_packet.data = (uint8_t*)malloc(packet_length);
                        if (rx_packet.data)
                        {
                            memcpy(rx_packet.data, rx_buffer, packet_length);
                            rx_packet.size = packet_length;
                            rx_packet.is_tx = false;
                            rx_packet.rssi = board_->getRadioRSSI();
                            rx_packet.snr = board_->getRadioSNR();

                            ++rx_summary.packets;
                            rx_summary.bytes += static_cast<uint32_t>(packet_length);
                            if (xQueueSend(mesh_queue_, &rx_packet, 0) != pdPASS)
                            {
                                free(rx_packet.data);
                                ++rx_summary.queue_drops;
                            }
                        }
                        else
                        {
                            ++rx_summary.alloc_drops;
                        }
                    }
                    else
                    {
                        ++rx_summary.read_failures;
                        board_->clearRadioIrqFlags(irq);
                    }
                }
                board_->clearRadioIrqFlags(irq);
                should_restart_rx = true;
            }
            else if (irq)
            {
                ++rx_summary.other_irqs;
                board_->clearRadioIrqFlags(irq);
                if ((irq & terminal_irq_mask) != 0)
                {
                    should_restart_rx = true;
                }
            }
            if (packet_length > 0 || should_restart_rx)
            {
                requestRadioReceiveRestart();
                int rx_state = board_->startRadioReceive();
                if (rx_state == RADIOLIB_ERR_NONE)
                {
                    setRadioReceiveActive(true);
                }
                else
                {
                    requestRadioReceiveRestart();
                    LORA_LOG("[LORA] RX restart fail state=%d\n", rx_state);
                }
            }
            maybe_log_radio_rx_summary(rx_summary);
        }

        vTaskDelay(display_spi_pressure_for_radio()
                       ? kRadioDisplayPressurePollDelay
                       : kRadioPollDelay);
    }
}

void AppTasks::meshTask(void* pvParameters)
{
    (void)pvParameters;

    const TickType_t poll_delay = pdMS_TO_TICKS(50);

    while (true)
    {
        // Process received packets
        RadioPacket rx_packet;
        if (::platform::ui::reticulum_call::realtime_mode_active())
        {
            while (xQueueReceive(mesh_queue_, &rx_packet, 0) == pdPASS)
            {
                if (rx_packet.data)
                {
                    free(rx_packet.data);
                }
            }
            if (adapter_)
            {
                adapter_->processSendQueue();
            }
            vTaskDelay(poll_delay);
            continue;
        }
        if (xQueueReceive(mesh_queue_, &rx_packet, 0) == pdPASS)
        {
            if (!rx_packet.is_tx && rx_packet.data && adapter_)
            {
                // Decode and process through configured mesh adapter
                adapter_->setLastRxStats(rx_packet.rssi, rx_packet.snr);
                adapter_->handleRawPacket(rx_packet.data, rx_packet.size);

                // Free buffer
                free(rx_packet.data);
            }
        }

        // Process send queue in adapter
        if (adapter_)
        {
            adapter_->processSendQueue();
        }

        vTaskDelay(poll_delay);
    }
}

} // namespace app
