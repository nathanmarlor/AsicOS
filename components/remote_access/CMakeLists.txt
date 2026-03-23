idf_component_register(
    SRCS "remote_access.c" "licence.c"
    INCLUDE_DIRS "include"
    REQUIRES "mqtt" "mbedtls" "nvs_config"
)
