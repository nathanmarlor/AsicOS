#include "adc_monitor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "adc_monitor";

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;
static bool s_initialized = false;

/**
 * Convert NTC thermistor voltage to temperature in Celsius.
 *
 * Circuit: Vcc (3.3V) -- R_pullup (10K) -- ADC node -- R_ntc (10K NTC) -- GND
 * NTC parameters: R0 = 10K at T0 = 25C, B = 3950
 */
static float ntc_voltage_to_temp(float voltage_mv)
{
    /* Voltage divider: V = Vcc * R_ntc / (R_pullup + R_ntc)
     * Solving for R_ntc: R_ntc = R_pullup * V / (Vcc - V) */
    float v = voltage_mv;
    if (v <= 0.0f || v >= 3300.0f) return -999.0f;

    float r_ntc = 10000.0f * v / (3300.0f - v);

    /* Steinhart-Hart simplified (B parameter equation):
     * 1/T = 1/T0 + (1/B) * ln(R/R0)
     * T0 = 25C = 298.15K, R0 = 10K, B = 3950 */
    float ln_r = logf(r_ntc / 10000.0f);
    float inv_t = (1.0f / 298.15f) + (1.0f / 3950.0f) * ln_r;

    return (1.0f / inv_t) - 273.15f;  /* Convert Kelvin to Celsius */
}

esp_err_t adc_monitor_init(void)
{
    if (s_initialized) return ESP_OK;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(err));
        return err;
    }

    /* Configure channels */
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,      /* 0-3.3V range */
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg); /* VCORE (ADC1_CH0) */
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_2, &chan_cfg); /* Therm2 (ADC1_CH2) */
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_3, &chan_cfg); /* Therm1 (ADC1_CH3) */

    /* Calibration via curve fitting */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration scheme failed: %s (readings may be less accurate)",
                 esp_err_to_name(err));
        /* Continue without calibration -- raw readings still work */
    }

    s_initialized = true;
    ESP_LOGI(TAG, "ADC monitor initialized (VCORE + 2x thermistor)");
    return ESP_OK;
}

esp_err_t adc_monitor_read(adc_readings_t *readings)
{
    if (!s_initialized || !readings) return ESP_ERR_INVALID_STATE;

    int raw;
    int mv;

    /* VCORE - channel 0 (ADC1_CH0)
     * The buck output goes through a 100K/100K resistor divider,
     * so actual voltage = ADC reading * 2.0 */
    if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_0, &raw) == ESP_OK) {
        if (s_cali_handle && adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
            readings->vcore_mv = (float)mv * 2.0f;
        } else {
            /* Fallback: rough conversion without calibration */
            readings->vcore_mv = ((float)raw / 4095.0f) * 3300.0f * 2.0f;
        }
    }

    /* Thermistor 1 (ASIC 1) - channel 3 (ADC1_CH3) */
    if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_3, &raw) == ESP_OK) {
        if (s_cali_handle && adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
            readings->therm1_temp_c = ntc_voltage_to_temp((float)mv);
        } else {
            readings->therm1_temp_c = ntc_voltage_to_temp(((float)raw / 4095.0f) * 3300.0f);
        }
    }

    /* Thermistor 2 (ASIC 2) - channel 2 (ADC1_CH2) */
    if (adc_oneshot_read(s_adc_handle, ADC_CHANNEL_2, &raw) == ESP_OK) {
        if (s_cali_handle && adc_cali_raw_to_voltage(s_cali_handle, raw, &mv) == ESP_OK) {
            readings->therm2_temp_c = ntc_voltage_to_temp((float)mv);
        } else {
            readings->therm2_temp_c = ntc_voltage_to_temp(((float)raw / 4095.0f) * 3300.0f);
        }
    }

    return ESP_OK;
}
