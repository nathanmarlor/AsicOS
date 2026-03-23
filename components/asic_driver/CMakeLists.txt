idf_component_register(
    SRCS "asic_packet.c" "asic.c" "bm1370.c"
    INCLUDE_DIRS "include"
    REQUIRES "serial"
)
