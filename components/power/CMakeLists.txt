idf_component_register(
    SRCS "pid.c" "voltage_regulator.c" "temp_sensor.c" "fan_controller.c" "emc2101.c" "ina260.c" "i2c_mux.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver"
)
