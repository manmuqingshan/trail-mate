#include "tm_ble.h"
#include "tm_c6_config.h"
#include "tm_diag.h"
#include "tm_espnow.h"
#include "tm_hostlink.h"
#include "tm_services.h"
#include "tm_wifi.h"
#include "tm_wifi_tcp.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* TAG = "C6_MAIN";

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS init returned %s, erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG,
             "Trail Mate C6 companion boot version=0x%08lx proto=%u..%u",
             (unsigned long)TM_C6_COMPANION_FIRMWARE_VERSION,
             TM_C6_PROTO_MIN,
             TM_C6_PROTO_MAX);

    init_nvs();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    tm_diag_init();
    ESP_ERROR_CHECK(tm_services_init());
    ESP_ERROR_CHECK(tm_wifi_init());
    ESP_ERROR_CHECK(tm_wifi_tcp_init());
    ESP_ERROR_CHECK(tm_espnow_init());
    ESP_ERROR_CHECK(tm_ble_init());
    tm_hostlink_init();
    tm_hostlink_start();

    ESP_LOGI(TAG, "C6 wireless services remain disabled until P4 HostLink config is received");
}
