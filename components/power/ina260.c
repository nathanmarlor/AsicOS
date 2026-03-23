#include "ina260.h"
#include "esp_log.h"

static const char *TAG = "ina260";

#define I2C_TIMEOUT_MS  100

/* INA260 register addresses */
#define REG_CURRENT     0x01
#define REG_VOLTAGE     0x02
#define REG_POWER       0x03

static ina260_config_t s_config;

static esp_err_t ina260_read_reg16(const ina260_config_t *config, uint8_t reg, uint16_t *val)
{
    uint8_t rx[2] = {0};
    esp_err_t err = i2c_master_write_read_device(config->port, config->address,
                                                  &reg, 1, rx, 2,
                                                  pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) return err;

    /* Big-endian: MSB first */
    *val = ((uint16_t)rx[0] << 8) | rx[1];
    return ESP_OK;
}

esp_err_t ina260_init(const ina260_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    ESP_LOGI(TAG, "INA260 init at 0x%02X on port %d", config->address, config->port);

    /* INA260 works with defaults - no configuration needed */
    return ESP_OK;
}

esp_err_t ina260_read(const ina260_config_t *config, ina260_reading_t *reading)
{
    if (!config || !reading) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw;
    esp_err_t err;

    /* Current register: signed, LSB = 1.25 mA */
    err = ina260_read_reg16(config, REG_CURRENT, &raw);
    if (err != ESP_OK) return err;
    reading->current_ma = (float)(int16_t)raw * 1.25f;

    /* Voltage register: unsigned, LSB = 1.25 mV */
    err = ina260_read_reg16(config, REG_VOLTAGE, &raw);
    if (err != ESP_OK) return err;
    reading->voltage_mv = (float)raw * 1.25f;

    /* Power register: unsigned, LSB = 10 mW */
    err = ina260_read_reg16(config, REG_POWER, &raw);
    if (err != ESP_OK) return err;
    reading->power_mw = (float)raw * 10.0f;

    return ESP_OK;
}
