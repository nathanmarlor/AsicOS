#include "emc2101.h"
#include "i2c_mux.h"
#include "esp_log.h"

static const char *TAG = "emc2101";

#define I2C_TIMEOUT_MS  100

/* EMC2101 register addresses */
#define REG_INTERNAL_TEMP   0x00
#define REG_EXTERNAL_TEMP   0x01
#define REG_CONFIG          0x03
#define REG_EXTERNAL_TEMP_LSB 0x10
#define REG_IDEALITY        0x17
#define REG_BETA            0x18
#define REG_FAN_CONFIG      0x4A
#define REG_FAN_SPINUP      0x4B
#define REG_FAN_SETTING     0x4C
#define REG_PWM_FREQ        0x4D
#define REG_TACH_LSB        0x46
#define REG_TACH_MSB        0x47

static esp_err_t emc2101_write_reg(const emc2101_config_t *config, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(config->port, config->dev_addr,
                                       buf, sizeof(buf),
                                       pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t emc2101_read_reg(const emc2101_config_t *config, uint8_t reg, uint8_t *val)
{
    return i2c_master_write_read_device(config->port, config->dev_addr,
                                         &reg, 1, val, 1,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

esp_err_t emc2101_init(const emc2101_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "EMC2101 init: mux=0x%02X ch=%d dev=0x%02X",
             config->mux_addr, config->mux_channel, config->dev_addr);

    esp_err_t err = i2c_mux_select(config->port, config->mux_addr, config->mux_channel);
    if (err != ESP_OK) return err;

    /* CONFIG: enable external diode, ALERT active low */
    err = emc2101_write_reg(config, REG_CONFIG, 0x04);
    if (err != ESP_OK) return err;

    /* FAN_CONFIG: DAC mode, PWM output */
    err = emc2101_write_reg(config, REG_FAN_CONFIG, 0x23);
    if (err != ESP_OK) return err;

    /* FAN_SPINUP: slow spin-up */
    err = emc2101_write_reg(config, REG_FAN_SPINUP, 0x68);
    if (err != ESP_OK) return err;

    /* PWM frequency */
    err = emc2101_write_reg(config, REG_PWM_FREQ, 0x1F);
    if (err != ESP_OK) return err;

    /* Ideality factor for external diode */
    err = emc2101_write_reg(config, REG_IDEALITY, 0x37);
    if (err != ESP_OK) return err;

    /* Beta compensation */
    err = emc2101_write_reg(config, REG_BETA, 0x00);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "EMC2101 ch%d initialized", config->mux_channel);
    return ESP_OK;
}

esp_err_t emc2101_read_external_temp(const emc2101_config_t *config, float *temp)
{
    if (!config || !temp) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_mux_select(config->port, config->mux_addr, config->mux_channel);
    if (err != ESP_OK) return err;

    uint8_t msb = 0, lsb = 0;

    err = emc2101_read_reg(config, REG_EXTERNAL_TEMP, &msb);
    if (err != ESP_OK) return err;

    err = emc2101_read_reg(config, REG_EXTERNAL_TEMP_LSB, &lsb);
    if (err != ESP_OK) return err;

    int16_t raw = (int16_t)(((uint16_t)msb << 8) | lsb) >> 5;

    /* Sign extend if negative (11-bit signed value) */
    if (raw & 0x0400) {
        raw |= 0xF800;
    }

    *temp = (float)raw / 8.0f;
    return ESP_OK;
}

esp_err_t emc2101_read_internal_temp(const emc2101_config_t *config, float *temp)
{
    if (!config || !temp) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_mux_select(config->port, config->mux_addr, config->mux_channel);
    if (err != ESP_OK) return err;

    uint8_t val = 0;
    err = emc2101_read_reg(config, REG_INTERNAL_TEMP, &val);
    if (err != ESP_OK) return err;

    *temp = (float)(int8_t)val;
    return ESP_OK;
}

esp_err_t emc2101_get_fan_rpm(const emc2101_config_t *config, uint16_t *rpm)
{
    if (!config || !rpm) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = i2c_mux_select(config->port, config->mux_addr, config->mux_channel);
    if (err != ESP_OK) return err;

    uint8_t lsb = 0, msb = 0;

    err = emc2101_read_reg(config, REG_TACH_LSB, &lsb);
    if (err != ESP_OK) {
        *rpm = 0;
        return err;
    }

    err = emc2101_read_reg(config, REG_TACH_MSB, &msb);
    if (err != ESP_OK) {
        *rpm = 0;
        return err;
    }

    uint16_t tach_count = ((uint16_t)msb << 8) | lsb;
    if (tach_count == 0 || tach_count == 0xFFFF) {
        *rpm = 0;
        return ESP_OK;
    }

    uint32_t calculated_rpm = 5400000UL / tach_count;
    if (calculated_rpm <= 82) {
        *rpm = 0;
    } else {
        *rpm = (uint16_t)calculated_rpm;
    }

    return ESP_OK;
}

esp_err_t emc2101_set_fan_duty(const emc2101_config_t *config, uint8_t percent)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        percent = 100;
    }

    esp_err_t err = i2c_mux_select(config->port, config->mux_addr, config->mux_channel);
    if (err != ESP_OK) return err;

    uint8_t val = (uint8_t)((uint16_t)percent * 63 / 100);
    return emc2101_write_reg(config, REG_FAN_SETTING, val);
}
