#pragma once
#include "esp_err.h"
#include <stdint.h>

typedef struct {
    float vcore_mv;        // Buck output voltage in mV
    float therm1_temp_c;   // Thermistor 1 temperature (ASIC 1)
    float therm2_temp_c;   // Thermistor 2 temperature (ASIC 2)
} adc_readings_t;

esp_err_t adc_monitor_init(void);
esp_err_t adc_monitor_read(adc_readings_t *readings);
