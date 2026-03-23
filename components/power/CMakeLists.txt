idf_component_register(
    SRCS "pid.c" "voltage_regulator.c" "temp_sensor.c" "fan_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver"
)
