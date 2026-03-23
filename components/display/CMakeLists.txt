idf_component_register(
    SRCS "display.c" "display_screens.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "esp_lcd" "nvs_config"
    PRIV_REQUIRES "power" "asic_driver" "mining"
)
