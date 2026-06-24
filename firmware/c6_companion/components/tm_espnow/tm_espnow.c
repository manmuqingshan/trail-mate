#include "tm_espnow.h"

#include "tm_services.h"
#include "tm_wifi.h"

#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <string.h>

static const char* TAG = "C6_ESPNOW";

enum
{
    TM_ESPNOW_RX_QUEUE_LEN = 8,
};

static tm_c6_espnow_config_t s_config;
static QueueHandle_t s_rx_queue;
static bool s_espnow_started;
static bool s_task_started;

static void emit_event(uint8_t kind, uint8_t status, const uint8_t mac[6], uint16_t error_code)
{
    tm_c6_espnow_event_t event = {
        .event_kind = kind,
        .status = status,
        .error_code = error_code,
    };
    if (mac != NULL)
    {
        memcpy(event.peer_mac, mac, sizeof(event.peer_mac));
    }
    (void)tm_services_send_espnow_event(&event);
}

static void recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int data_len)
{
    if (data == NULL || data_len < 0 || data_len > TM_C6_ESPNOW_PAYLOAD_MAX || s_rx_queue == NULL)
    {
        tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "espnow_rx_too_large");
        return;
    }
    if (!tm_services_can_accept_wireless_rx("espnow_low_memory"))
    {
        emit_event(TM_C6_ESPNOW_EVENT_RECV_DROPPED,
                   1,
                   info != NULL ? info->src_addr : NULL,
                   TM_C6_ERROR_LOW_MEMORY);
        return;
    }

    tm_c6_espnow_packet_t packet = {};
    if (info != NULL && info->src_addr != NULL)
    {
        memcpy(packet.peer_mac, info->src_addr, sizeof(packet.peer_mac));
    }
    if (info != NULL && info->rx_ctrl != NULL)
    {
        packet.rssi_valid = 1;
        packet.rssi = info->rx_ctrl->rssi;
        packet.channel = info->rx_ctrl->channel;
    }
    packet.payload_len = (uint8_t)data_len;
    if (data_len > 0)
    {
        memcpy(packet.payload, data, (size_t)data_len);
    }

    if (xQueueSend(s_rx_queue, &packet, 0) != pdTRUE)
    {
        tm_services_record_error(TM_C6_ERROR_QUEUE_FULL, "espnow_rx_queue_full");
        emit_event(TM_C6_ESPNOW_EVENT_RECV_DROPPED,
                   1,
                   info != NULL ? info->src_addr : NULL,
                   TM_C6_ERROR_QUEUE_FULL);
    }
}

static void emit_send_done(const uint8_t* mac, esp_now_send_status_t status)
{
    emit_event(TM_C6_ESPNOW_EVENT_SEND_DONE,
               status == ESP_NOW_SEND_SUCCESS ? 0 : 1,
               mac,
               status == ESP_NOW_SEND_SUCCESS ? TM_C6_OK : TM_C6_ERROR_INTERNAL);
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void send_cb(const esp_now_send_info_t* tx_info, esp_now_send_status_t status)
{
    emit_send_done(tx_info != NULL ? tx_info->des_addr : NULL, status);
}
#else
static void send_cb(const uint8_t* mac_addr, esp_now_send_status_t status)
{
    emit_send_done(mac_addr, status);
}
#endif

static void rx_task(void* arg)
{
    (void)arg;
    tm_c6_espnow_packet_t packet = {};
    for (;;)
    {
        if (xQueueReceive(s_rx_queue, &packet, portMAX_DELAY) == pdTRUE)
        {
            (void)tm_services_send_espnow_uplink(&packet);
        }
    }
}

static esp_err_t ensure_peer(const uint8_t mac[6], uint8_t channel)
{
    if (mac == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (esp_now_is_peer_exist(mac))
    {
        return ESP_OK;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = channel;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err == ESP_ERR_ESPNOW_EXIST)
    {
        return ESP_OK;
    }
    return err;
}

static esp_err_t ensure_started(void)
{
    if (s_espnow_started)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(tm_wifi_ensure_radio_started(), TAG, "wifi_radio");
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(recv_cb), TAG, "esp_now_recv_cb");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(send_cb), TAG, "esp_now_send_cb");

    if (s_rx_queue == NULL)
    {
        s_rx_queue = xQueueCreate(TM_ESPNOW_RX_QUEUE_LEN, sizeof(tm_c6_espnow_packet_t));
        if (s_rx_queue == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!s_task_started)
    {
        if (xTaskCreate(rx_task, "tm_espnow_rx", 4096, NULL, 8, NULL) != pdPASS)
        {
            return ESP_ERR_NO_MEM;
        }
        s_task_started = true;
    }

    s_espnow_started = true;
    emit_event(TM_C6_ESPNOW_EVENT_STARTED, 0, NULL, TM_C6_OK);
    return ESP_OK;
}

esp_err_t tm_espnow_init(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_rx_queue = xQueueCreate(TM_ESPNOW_RX_QUEUE_LEN, sizeof(tm_c6_espnow_packet_t));
    if (s_rx_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t tm_espnow_apply_config(const tm_c6_espnow_config_t* config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    if (!s_config.espnow_enabled)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(ensure_started(), TAG, "espnow_start");
    const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t* peer = s_config.broadcast_mac[0] || s_config.broadcast_mac[1] ||
                                  s_config.broadcast_mac[2] || s_config.broadcast_mac[3] ||
                                  s_config.broadcast_mac[4] || s_config.broadcast_mac[5]
                              ? s_config.broadcast_mac
                              : broadcast;
    return ensure_peer(peer, s_config.channel);
}

esp_err_t tm_espnow_send_packet(const tm_c6_espnow_packet_t* packet)
{
    if (packet == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (packet->payload_len > TM_C6_ESPNOW_PAYLOAD_MAX)
    {
        tm_services_record_error(TM_C6_ERROR_PAYLOAD_TOO_LARGE, "espnow_tx_too_large");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_RETURN_ON_ERROR(ensure_started(), TAG, "espnow_send_start");
    const uint8_t channel = packet->channel ? packet->channel : s_config.channel;
    ESP_RETURN_ON_ERROR(ensure_peer(packet->peer_mac, channel), TAG, "espnow_peer");
    ESP_RETURN_ON_ERROR(esp_now_send(packet->peer_mac, packet->payload, packet->payload_len), TAG, "espnow_send");
    tm_services_note_espnow_tx();
    return ESP_OK;
}
