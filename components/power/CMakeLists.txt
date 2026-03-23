idf_component_register(
    SRCS "pid.c" "voltage_regulator.c" "temp_sensor.c" "fan_controller.c" "emc2101.c" "ina260.c" "i2c_mux.c"
         "adc_monitor.c" "led_status.c" "plug_sense.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "esp_adc" "esp_timer"
)
