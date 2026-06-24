#include "tm_diag.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char* TAG = "C6_DIAG";

enum
{
    TM_C6_LOG_RING_SIZE = 8192,
};

static char s_log_ring[TM_C6_LOG_RING_SIZE];
static size_t s_log_head;
static size_t s_log_tail;
static size_t s_log_used;
static uint32_t s_dropped_log_bytes;
static portMUX_TYPE s_log_lock = portMUX_INITIALIZER_UNLOCKED;

static void push_log_byte(char ch)
{
    if (s_log_used == sizeof(s_log_ring))
    {
        s_log_tail = (s_log_tail + 1u) % sizeof(s_log_ring);
        --s_log_used;
        ++s_dropped_log_bytes;
    }

    s_log_ring[s_log_head] = ch;
    s_log_head = (s_log_head + 1u) % sizeof(s_log_ring);
    ++s_log_used;
}

esp_err_t tm_diag_init(void)
{
    portENTER_CRITICAL(&s_log_lock);
    s_log_head = 0;
    s_log_tail = 0;
    s_log_used = 0;
    s_dropped_log_bytes = 0;
    portEXIT_CRITICAL(&s_log_lock);
    ESP_LOGI(TAG, "diag initialized free_heap=%lu", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return ESP_OK;
}

void tm_diag_record_log(const char* message)
{
    if (message == NULL || message[0] == '\0')
    {
        return;
    }

    portENTER_CRITICAL(&s_log_lock);
    for (size_t i = 0; message[i] != '\0'; ++i)
    {
        push_log_byte(message[i]);
    }
    push_log_byte('\n');
    portEXIT_CRITICAL(&s_log_lock);
}

bool tm_diag_pop_log(char* out, size_t out_len)
{
    if (out == NULL || out_len == 0)
    {
        return false;
    }

    portENTER_CRITICAL(&s_log_lock);
    if (s_log_used == 0)
    {
        portEXIT_CRITICAL(&s_log_lock);
        out[0] = '\0';
        return false;
    }

    size_t written = 0;
    while (s_log_used > 0 && written + 1u < out_len)
    {
        const char ch = s_log_ring[s_log_tail];
        s_log_tail = (s_log_tail + 1u) % sizeof(s_log_ring);
        --s_log_used;
        if (ch == '\n')
        {
            break;
        }
        out[written++] = ch;
    }
    out[written] = '\0';
    portEXIT_CRITICAL(&s_log_lock);
    return written > 0;
}

uint32_t tm_diag_dropped_log_bytes(void)
{
    uint32_t dropped = 0;
    portENTER_CRITICAL(&s_log_lock);
    dropped = s_dropped_log_bytes;
    portEXIT_CRITICAL(&s_log_lock);
    return dropped;
}
