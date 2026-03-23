idf_component_register(
    SRCS "main.c"
          "boards/asicos_v1.c"
          "tasks/wifi_task.c"
          "http_server/http_server.c"
          "http_server/api_system.c"
          "http_server/api_mining.c"
          "http_server/api_tuner.c"
          "http_server/api_remote.c"
          "http_server/ws_handler.c"
    INCLUDE_DIRS "." "tasks" "http_server"
    REQUIRES "nvs_flash" "esp_wifi" "esp_http_server" "spiffs" "json" "app_update" "self_test"
)
