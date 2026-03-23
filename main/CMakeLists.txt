idf_component_register(
    SRCS "main.c"
          "boards/asicos_v1.c"
          "tasks/wifi_task.c"
    INCLUDE_DIRS "." "tasks"
    REQUIRES "nvs_flash" "esp_wifi" "esp_http_server" "spiffs"
)
