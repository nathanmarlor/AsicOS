idf_component_register(
    SRCS "sha256.c" "mining.c"
    INCLUDE_DIRS "include"
    REQUIRES "mbedtls"
)
