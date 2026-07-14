#include "tm_hostlink_sdio.h"

#include "driver/sdio_slave.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char* TAG = "C6_SDIO";

enum
{
    TM_C6_SDIO_BUFFER_SIZE = 1152,
    TM_C6_SDIO_RX_BUFFER_NUM = 16,
    TM_C6_SDIO_TX_BUFFER_NUM = 4,
    TM_C6_SDIO_QUEUE_SIZE = 16,
};

static uint8_t* s_rx_buffers[TM_C6_SDIO_RX_BUFFER_NUM];
static sdio_slave_buf_handle_t s_rx_handles[TM_C6_SDIO_RX_BUFFER_NUM];
static uint8_t* s_tx_buffers[TM_C6_SDIO_TX_BUFFER_NUM];
static bool s_tx_busy[TM_C6_SDIO_TX_BUFFER_NUM];
static bool s_initialized = false;
static SemaphoreHandle_t s_tx_mutex;

static TickType_t ticks_from_ms(uint32_t timeout_ms)
{
    if (timeout_ms == UINT32_MAX)
    {
        return portMAX_DELAY;
    }
    return pdMS_TO_TICKS(timeout_ms);
}

static uint8_t* alloc_dma_buffer(void)
{
    return (uint8_t*)heap_caps_calloc(1, TM_C6_SDIO_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

esp_err_t tm_hostlink_sdio_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    if (s_tx_mutex == NULL)
    {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    sdio_slave_config_t config = {
        .sending_mode = SDIO_SLAVE_SEND_PACKET,
        .send_queue_size = TM_C6_SDIO_QUEUE_SIZE,
        .recv_buffer_size = TM_C6_SDIO_BUFFER_SIZE,
        .event_cb = NULL,
        .flags = 0,
    };

    esp_err_t err = sdio_slave_initialize(&config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sdio_slave_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    for (size_t i = 0; i < TM_C6_SDIO_RX_BUFFER_NUM; ++i)
    {
        s_rx_buffers[i] = alloc_dma_buffer();
        if (s_rx_buffers[i] == NULL)
        {
            ESP_LOGE(TAG, "rx DMA buffer allocation failed at %u", (unsigned)i);
            return ESP_ERR_NO_MEM;
        }

        s_rx_handles[i] = sdio_slave_recv_register_buf(s_rx_buffers[i]);
        if (s_rx_handles[i] == NULL)
        {
            ESP_LOGE(TAG, "sdio rx register failed at %u", (unsigned)i);
            return ESP_ERR_NO_MEM;
        }

        err = sdio_slave_recv_load_buf(s_rx_handles[i]);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "sdio rx load failed at %u: %s", (unsigned)i, esp_err_to_name(err));
            return err;
        }
    }

    for (size_t i = 0; i < TM_C6_SDIO_TX_BUFFER_NUM; ++i)
    {
        s_tx_buffers[i] = alloc_dma_buffer();
        if (s_tx_buffers[i] == NULL)
        {
            ESP_LOGE(TAG, "tx DMA buffer allocation failed at %u", (unsigned)i);
            return ESP_ERR_NO_MEM;
        }
    }

    sdio_slave_set_host_intena(SDIO_SLAVE_HOSTINT_SEND_NEW_PACKET);
    err = sdio_slave_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "sdio_slave_start failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG,
             "SDIO slave ready rx_buf=%u rx_count=%u tx_count=%u queue=%u",
             TM_C6_SDIO_BUFFER_SIZE,
             TM_C6_SDIO_RX_BUFFER_NUM,
             TM_C6_SDIO_TX_BUFFER_NUM,
             TM_C6_SDIO_QUEUE_SIZE);
    return ESP_OK;
}

static void poll_tx_done_locked(void)
{
    void* arg = NULL;
    while (sdio_slave_send_get_finished(&arg, 0) == ESP_OK)
    {
        const uintptr_t index = (uintptr_t)arg;
        if (index < TM_C6_SDIO_TX_BUFFER_NUM)
        {
            s_tx_busy[index] = false;
            ESP_LOGI(TAG, "tx done index=%u", (unsigned)index);
        }
    }
}

void tm_hostlink_sdio_poll_tx_done(void)
{
    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, portMAX_DELAY) != pdTRUE)
    {
        return;
    }
    poll_tx_done_locked();
    xSemaphoreGive(s_tx_mutex);
}

esp_err_t tm_hostlink_sdio_recv(uint8_t* data, size_t max_len, size_t* out_len, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || out_len == NULL || max_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    *out_len = 0;

    sdio_slave_buf_handle_t handle = NULL;
    esp_err_t err = sdio_slave_recv_packet(&handle, ticks_from_ms(timeout_ms));
    if (err != ESP_OK)
    {
        return err;
    }

    size_t len = 0;
    uint8_t* ptr = sdio_slave_recv_get_buf(handle, &len);
    if (ptr == NULL)
    {
        (void)sdio_slave_recv_load_buf(handle);
        return ESP_ERR_INVALID_STATE;
    }
    if (len > max_len)
    {
        (void)sdio_slave_recv_load_buf(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(data, ptr, len);
    *out_len = len;
    return sdio_slave_recv_load_buf(handle);
}

esp_err_t tm_hostlink_sdio_send(const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    if (!s_initialized || data == NULL || len == 0 || len > TM_C6_SDIO_BUFFER_SIZE)
    {
        ESP_LOGW(TAG,
                 "send invalid initialized=%u data=%p len=%u max=%u",
                 s_initialized ? 1u : 0u,
                 data,
                 (unsigned)len,
                 (unsigned)TM_C6_SDIO_BUFFER_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_tx_mutex == NULL || xSemaphoreTake(s_tx_mutex, ticks_from_ms(timeout_ms)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    poll_tx_done_locked();

    size_t tx_index = TM_C6_SDIO_TX_BUFFER_NUM;
    for (size_t i = 0; i < TM_C6_SDIO_TX_BUFFER_NUM; ++i)
    {
        if (!s_tx_busy[i])
        {
            tx_index = i;
            break;
        }
    }
    if (tx_index == TM_C6_SDIO_TX_BUFFER_NUM)
    {
        ESP_LOGW(TAG, "send no free tx buffer len=%u timeout_ms=%lu", (unsigned)len, (unsigned long)timeout_ms);
        xSemaphoreGive(s_tx_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    memcpy(s_tx_buffers[tx_index], data, len);
    s_tx_busy[tx_index] = true;
    ESP_LOGI(TAG,
             "send queue begin index=%u len=%u timeout_ms=%lu",
             (unsigned)tx_index,
             (unsigned)len,
             (unsigned long)timeout_ms);
    const esp_err_t err =
        sdio_slave_send_queue(s_tx_buffers[tx_index], len, (void*)(uintptr_t)tx_index, ticks_from_ms(timeout_ms));
    if (err != ESP_OK)
    {
        s_tx_busy[tx_index] = false;
        ESP_LOGW(TAG,
                 "send queue failed index=%u len=%u err=%s",
                 (unsigned)tx_index,
                 (unsigned)len,
                 esp_err_to_name(err));
        xSemaphoreGive(s_tx_mutex);
        return err;
    }
    ESP_LOGI(TAG, "send queue ok index=%u len=%u", (unsigned)tx_index, (unsigned)len);
    const esp_err_t int_err = sdio_slave_send_host_int(0);
    if (int_err != ESP_OK)
    {
        ESP_LOGW(TAG, "send host int failed index=%u err=%s", (unsigned)tx_index, esp_err_to_name(int_err));
    }
    else
    {
        ESP_LOGI(TAG, "send host int ok index=%u", (unsigned)tx_index);
    }
    xSemaphoreGive(s_tx_mutex);
    return ESP_OK;
}
