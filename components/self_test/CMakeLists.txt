idf_component_register(
    SRCS "self_test.c"
    INCLUDE_DIRS "include"
    REQUIRES "serial" "asic_driver" "power" "nvs_config" "mining"
)
