idf_component_register(
    SRCS "tm_wifi.c"
    INCLUDE_DIRS
        "include"
        "../../../../modules/core_hostlink/include"
    REQUIRES
        tm_services
        esp_wifi
        esp_netif
        esp_event
        esp_timer
        lwip
)
