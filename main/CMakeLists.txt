if(CONFIG_ASICOS_BOARD_NERDQAXEPP)
    set(BOARD_SRC "boards/nerdqaxepp.c")
elseif(CONFIG_ASICOS_BOARD_BITFORGE_NANO)
    set(BOARD_SRC "boards/bitforge_nano.c")
else()
    set(BOARD_SRC "boards/asicos_v1.c")
endif()

idf_component_register(
    SRCS "main.c"
          "${BOARD_SRC}"
          "tasks/wifi_task.c"
          "tasks/mining_task.c"
          "tasks/result_task.c"
          "tasks/hashrate_task.c"
          "tasks/power_task.c"
          "tasks/tuner_task.c"
          "tasks/remote_task.c"
          "tasks/loki_task.c"
          "http_server/http_server.c"
          "http_server/api_system.c"
          "http_server/api_mining.c"
          "http_server/api_tuner.c"
          "http_server/api_remote.c"
          "http_server/api_metrics.c"
          "http_server/ws_handler.c"
          "display_screens.c"
    INCLUDE_DIRS "." "tasks" "http_server"
    REQUIRES "nvs_flash"
             "esp_wifi"
             "esp_http_server"
             "esp_http_client"
             "spiffs"
             "driver"
             "serial"
             "asic_driver"
             "stratum"
             "power"
             "mining"
             "nvs_config"
             "auto_tuner"
             "self_test"
             "remote_access"
             "esp_app_format"
             "app_update"
             "json"
             "display"
             "esp_lcd"
)
