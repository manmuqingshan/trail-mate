#include "tm_wifi_tcp.h"

#include "hostlink/c6/c6_protocol.h"
#include "tm_services.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/errno.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "C6_WIFI_TCP";

enum
{
    TM_WIFI_TCP_QUEUE_DEPTH = 4,
    TM_WIFI_TCP_TASK_STACK_BYTES = 6144,
    TM_WIFI_TCP_TASK_PRIORITY = 7,
    TM_WIFI_TCP_POLL_MS = 10,
    TM_WIFI_TCP_CONNECT_TIMEOUT_SECONDS = 6,
};

typedef struct tm_wifi_tcp_command
{
    tm_c6_wifi_tcp_header_t header;
    uint8_t payload[TM_C6_WIFI_TCP_PAYLOAD_MAX];
} tm_wifi_tcp_command_t;

static QueueHandle_t s_command_queue;
static TaskHandle_t s_worker_task;
static int s_socket = -1;
static uint8_t s_connection_id;
static uint8_t s_event_payload[TM_C6_MAX_PAYLOAD];
static uint8_t s_rx_payload[TM_C6_WIFI_TCP_PAYLOAD_MAX];
static tm_wifi_tcp_command_t s_worker_command;
static tm_wifi_tcp_command_t s_enqueue_command;
static size_t s_pending_event_len;

static bool flush_pending_event(void)
{
    if (s_pending_event_len == 0)
    {
        return true;
    }
    if (!tm_services_send_wifi_data(s_event_payload, s_pending_event_len))
    {
        return false;
    }
    s_pending_event_len = 0;
    return true;
}

static void close_socket(void)
{
    if (s_socket >= 0)
    {
        shutdown(s_socket, SHUT_RDWR);
        close(s_socket);
        s_socket = -1;
    }
}

static bool emit_event(uint8_t operation,
                       uint8_t connection_id,
                       uint16_t error_code,
                       const uint8_t* data,
                       size_t data_len)
{
    if (data_len > TM_C6_WIFI_TCP_PAYLOAD_MAX || (data == NULL && data_len != 0))
    {
        return false;
    }
    if (s_pending_event_len != 0)
    {
        return false;
    }

    tm_c6_wifi_tcp_header_t header = {
        .operation = operation,
        .connection_id = connection_id,
        .payload_len = (uint16_t)data_len,
        .port = 0,
        .error_code = error_code,
    };
    memcpy(s_event_payload, &header, sizeof(header));
    if (data_len > 0)
    {
        memcpy(s_event_payload + sizeof(header), data, data_len);
    }
    s_pending_event_len = sizeof(header) + data_len;
    (void)flush_pending_event();
    return true;
}

static void fail_connection(uint16_t error_code, const char* detail)
{
    const uint8_t connection_id = s_connection_id;
    close_socket();
    tm_services_record_error(error_code, detail);
    (void)emit_event(TM_C6_WIFI_TCP_ERROR, connection_id, error_code, NULL, 0);
}

static bool connect_with_timeout(int socket_fd,
                                 const struct sockaddr* address,
                                 socklen_t address_len)
{
    const int original_flags = fcntl(socket_fd, F_GETFL, 0);
    if (original_flags < 0 ||
        fcntl(socket_fd, F_SETFL, original_flags | O_NONBLOCK) != 0)
    {
        return false;
    }

    bool connected = connect(socket_fd, address, address_len) == 0;
    if (!connected && errno == EINPROGRESS)
    {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(socket_fd, &write_set);
        struct timeval timeout = {
            .tv_sec = TM_WIFI_TCP_CONNECT_TIMEOUT_SECONDS,
            .tv_usec = 0,
        };
        const int selected = select(socket_fd + 1, NULL, &write_set, NULL, &timeout);
        if (selected > 0 && FD_ISSET(socket_fd, &write_set))
        {
            int socket_error = 0;
            socklen_t error_len = sizeof(socket_error);
            connected = getsockopt(socket_fd,
                                   SOL_SOCKET,
                                   SO_ERROR,
                                   &socket_error,
                                   &error_len) == 0 &&
                        socket_error == 0;
        }
    }

    if (fcntl(socket_fd, F_SETFL, original_flags) != 0)
    {
        connected = false;
    }
    return connected;
}

static esp_err_t open_socket(const tm_wifi_tcp_command_t* command)
{
    if (command->header.host[0] == '\0' || command->header.port == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    close_socket();
    s_connection_id = command->header.connection_id;

    char port_text[6] = {};
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)command->header.port);
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo* result = NULL;
    const int lookup = getaddrinfo(command->header.host, port_text, &hints, &result);
    if (lookup != 0 || result == NULL)
    {
        ESP_LOGW(TAG, "resolve failed host=%s code=%d", command->header.host, lookup);
        if (result != NULL)
        {
            freeaddrinfo(result);
        }
        return ESP_ERR_NOT_FOUND;
    }

    int connected_socket = -1;
    for (const struct addrinfo* address = result; address != NULL; address = address->ai_next)
    {
        const int candidate = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (candidate < 0)
        {
            continue;
        }

        if (connect_with_timeout(candidate, address->ai_addr, address->ai_addrlen))
        {
            connected_socket = candidate;
            break;
        }
        close(candidate);
    }
    freeaddrinfo(result);

    if (connected_socket < 0)
    {
        ESP_LOGW(TAG,
                 "connect failed host=%s port=%u errno=%d",
                 command->header.host,
                 (unsigned)command->header.port,
                 errno);
        return ESP_ERR_INVALID_STATE;
    }

    s_socket = connected_socket;
    ESP_LOGI(TAG,
             "connected id=%u host=%s port=%u",
             (unsigned)s_connection_id,
             command->header.host,
             (unsigned)command->header.port);
    (void)emit_event(TM_C6_WIFI_TCP_OPENED, s_connection_id, TM_C6_OK, NULL, 0);
    return ESP_OK;
}

static esp_err_t write_socket(const tm_wifi_tcp_command_t* command)
{
    if (s_socket < 0 || command->header.connection_id != s_connection_id)
    {
        return ESP_ERR_INVALID_STATE;
    }

    size_t sent = 0;
    while (sent < command->header.payload_len)
    {
        const int result = send(s_socket,
                                command->payload + sent,
                                command->header.payload_len - sent,
                                0);
        if (result <= 0)
        {
            return ESP_ERR_INVALID_STATE;
        }
        sent += (size_t)result;
    }
    return ESP_OK;
}

static void process_command(const tm_wifi_tcp_command_t* command)
{
    switch (command->header.operation)
    {
    case TM_C6_WIFI_TCP_OPEN:
        if (open_socket(command) != ESP_OK)
        {
            fail_connection(TM_C6_ERROR_NOT_CONNECTED, "wifi_tcp_open_failed");
        }
        break;
    case TM_C6_WIFI_TCP_WRITE:
        if (write_socket(command) != ESP_OK)
        {
            fail_connection(TM_C6_ERROR_NOT_CONNECTED, "wifi_tcp_write_failed");
        }
        break;
    case TM_C6_WIFI_TCP_CLOSE:
    {
        const uint8_t connection_id = command->header.connection_id;
        close_socket();
        (void)emit_event(TM_C6_WIFI_TCP_CLOSED, connection_id, TM_C6_OK, NULL, 0);
        break;
    }
    default:
        tm_services_record_error(TM_C6_ERROR_UNSUPPORTED_FRAME, "wifi_tcp_operation_invalid");
        break;
    }
}

static void poll_socket(void)
{
    if (s_socket < 0)
    {
        return;
    }

    const int received = recv(s_socket, s_rx_payload, sizeof(s_rx_payload), MSG_DONTWAIT);
    if (received > 0)
    {
        (void)emit_event(TM_C6_WIFI_TCP_DATA,
                         s_connection_id,
                         TM_C6_OK,
                         s_rx_payload,
                         (size_t)received);
        return;
    }
    if (received == 0)
    {
        const uint8_t connection_id = s_connection_id;
        close_socket();
        (void)emit_event(TM_C6_WIFI_TCP_CLOSED, connection_id, TM_C6_OK, NULL, 0);
        return;
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
        fail_connection(TM_C6_ERROR_NOT_CONNECTED, "wifi_tcp_receive_failed");
    }
}

static void wifi_tcp_task(void* arg)
{
    (void)arg;
    for (;;)
    {
        if (!flush_pending_event())
        {
            vTaskDelay(pdMS_TO_TICKS(TM_WIFI_TCP_POLL_MS));
            continue;
        }
        if (xQueueReceive(s_command_queue,
                          &s_worker_command,
                          pdMS_TO_TICKS(TM_WIFI_TCP_POLL_MS)) == pdTRUE)
        {
            process_command(&s_worker_command);
        }
        poll_socket();
    }
}

esp_err_t tm_wifi_tcp_init(void)
{
    if (s_worker_task != NULL)
    {
        return ESP_OK;
    }

    s_pending_event_len = 0;

    s_command_queue = xQueueCreate(TM_WIFI_TCP_QUEUE_DEPTH, sizeof(tm_wifi_tcp_command_t));
    if (s_command_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(wifi_tcp_task,
                    "tm_wifi_tcp",
                    TM_WIFI_TCP_TASK_STACK_BYTES,
                    NULL,
                    TM_WIFI_TCP_TASK_PRIORITY,
                    &s_worker_task) != pdPASS)
    {
        vQueueDelete(s_command_queue);
        s_command_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t tm_wifi_tcp_handle_frame(const uint8_t* payload, size_t payload_len)
{
    if (payload == NULL || payload_len < sizeof(tm_c6_wifi_tcp_header_t))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_enqueue_command, 0, sizeof(s_enqueue_command));
    memcpy(&s_enqueue_command.header, payload, sizeof(s_enqueue_command.header));
    const size_t data_len = payload_len - sizeof(s_enqueue_command.header);
    if (s_enqueue_command.header.payload_len != data_len ||
        data_len > sizeof(s_enqueue_command.payload) ||
        (s_enqueue_command.header.operation != TM_C6_WIFI_TCP_OPEN &&
         s_enqueue_command.header.operation != TM_C6_WIFI_TCP_WRITE &&
         s_enqueue_command.header.operation != TM_C6_WIFI_TCP_CLOSE))
    {
        return ESP_ERR_INVALID_ARG;
    }
    if ((s_enqueue_command.header.operation == TM_C6_WIFI_TCP_OPEN &&
         (data_len != 0 || s_enqueue_command.header.host[0] == '\0' ||
          s_enqueue_command.header.port == 0)) ||
        (s_enqueue_command.header.operation == TM_C6_WIFI_TCP_WRITE && data_len == 0) ||
        (s_enqueue_command.header.operation == TM_C6_WIFI_TCP_CLOSE && data_len != 0))
    {
        return ESP_ERR_INVALID_ARG;
    }
    s_enqueue_command.header.host[sizeof(s_enqueue_command.header.host) - 1] = '\0';
    if (data_len > 0)
    {
        memcpy(s_enqueue_command.payload,
               payload + sizeof(s_enqueue_command.header),
               data_len);
    }
    if (s_command_queue == NULL ||
        xQueueSend(s_command_queue, &s_enqueue_command, 0) != pdTRUE)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
