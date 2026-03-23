idf_component_register(
    SRCS "main.c"
          "boards/asicos_v1.c"
    INCLUDE_DIRS "."
    REQUIRES "nvs_flash" "esp_wifi" "esp_http_server" "spiffs"
)
