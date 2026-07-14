#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t tm_wifi_tcp_init(void);
    esp_err_t tm_wifi_tcp_handle_frame(const uint8_t* payload, size_t payload_len);

#ifdef __cplusplus
}
#endif
