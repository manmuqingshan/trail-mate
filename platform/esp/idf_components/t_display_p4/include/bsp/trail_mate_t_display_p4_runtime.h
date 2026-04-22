#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    bool trail_mate_t_display_p4_display_runtime_init(void);
    bool trail_mate_t_display_p4_display_runtime_is_ready(void);
    bool trail_mate_t_display_p4_display_lock(uint32_t timeout_ms);
    void trail_mate_t_display_p4_display_unlock(void);
    esp_err_t trail_mate_t_display_p4_display_set_brightness_percent(int brightness_percent);

#ifdef __cplusplus
}
#endif
