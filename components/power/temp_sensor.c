#include <string.h>
#include "temp_sensor.h"
#include "emc2101.h"
#include "i2c_mux.h"
#include "esp_log.h"

static const char *TAG = "temp_sensor";

#define TMP1075_TEMP_REG  0x00
#define I2C_TIMEOUT_MS    100

static temp_sensor_config_t s_config;
static emc2101_config_t s_emc2101[TEMP_SENSOR_MAX_COUNT];

esp_err_t temp_sensor_init(const temp_sensor_config_t *config)
{
    if (!config || config->count == 0 || config->count > TEMP_SENSOR_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    ESP_LOGI(TAG, "temp sensor init, %d sensors, type=%d", config->count, config->type);

    if (config->type == TEMP_TYPE_EMC2101) {
        esp_err_t err = i2c_mux_init(config->port, config->mux_addr);
        if (err != ESP_OK) return err;

        for (int i = 0; i < config->count; i++) {
            s_emc2101[i].port = config->port;
            s_emc2101[i].mux_addr = config->mux_addr;
            s_emc2101[i].mux_channel = config->mux_channels[i];
            s_emc2101[i].dev_addr = config->addresses[i];

            err = emc2101_init(&s_emc2101[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "EMC2101 sensor %d init failed: %s", i, esp_err_to_name(err));
                return err;
            }
        }
    }

    return ESP_OK;
}

esp_err_t temp_sensor_read(uint8_t index, float *temperature)
{
    if (index >= s_config.count || !temperature) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_config.type == TEMP_TYPE_EMC2101) {
        return emc2101_read_external_temp(&s_emc2101[index], temperature);
    }

    /* TMP1075 path */
    uint8_t reg = TMP1075_TEMP_REG;
    uint8_t rx[2] = {0};

    esp_err_t err = i2c_master_write_read_device(s_config.port,
                                                  s_config.addresses[index],
                                                  &reg, 1, rx, 2,
                                                  pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        *temperature = 0.0f;
        ESP_LOGD(TAG, "sensor %d read failed: 0x%X", index, err);
        return err;
    }

    /* TMP1075: 12-bit result in upper bits, 0.0625 per LSB */
    int16_t raw = (int16_t)((rx[0] << 8) | rx[1]);
    *temperature = (float)(raw >> 4) * 0.0625f;
    return ESP_OK;
}

esp_err_t temp_sensor_read_max(float *temperature)
{
    if (!temperature) {
        return ESP_ERR_INVALID_ARG;
    }

    float max_temp = -273.15f;
    bool any_ok = false;

    for (uint8_t i = 0; i < s_config.count; i++) {
        float t;
        esp_err_t err = temp_sensor_read(i, &t);
        if (err == ESP_OK) {
            any_ok = true;
            if (t > max_temp) {
                max_temp = t;
            }
        } else {
            ESP_LOGW(TAG, "failed to read sensor %d: 0x%X", i, err);
        }
    }

    if (!any_ok) {
        return ESP_FAIL;
    }

    *temperature = max_temp;
    return ESP_OK;
}
