#include "esp_bt.h"
#include "esp_err.h"

#ifndef TRAIL_MATE_ENABLE_BLE
#define TRAIL_MATE_ENABLE_BLE 0
#endif

#if !TRAIL_MATE_ENABLE_BLE
extern "C" bool btInUse()
{
    return true;
}

extern "C" esp_err_t esp_bt_controller_mem_release(esp_bt_mode_t mode)
{
    (void)mode;
    return ESP_OK;
}
#endif
