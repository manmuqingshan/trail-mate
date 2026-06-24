#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t tm_diag_init(void);
    void tm_diag_record_log(const char* message);
    bool tm_diag_pop_log(char* out, size_t out_len);
    uint32_t tm_diag_dropped_log_bytes(void);

#ifdef __cplusplus
}
#endif
