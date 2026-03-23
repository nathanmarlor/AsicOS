#include "fan_controller.h"
#include "esp_log.h"

static const char *TAG = "fan";

/* EMC2302 registers */
#define FAN1_SETTING  0x30
#define FAN2_SETTING  0x40
#define FAN1_TACH_HI  0x3E
#define FAN1_TACH_LO  0x3F
#define FAN2_TACH_HI  0x4E
#define FAN2_TACH_LO  0x4F

#define I2C_TIMEOUT_MS 100

static fan_config_t s_config;

static uint8_t fan_setting_reg(uint8_t channel)
{
    return (channel == 0) ? FAN1_SETTING : FAN2_SETTING;
}

static void fan_tach_regs(uint8_t channel, uint8_t *hi_reg, uint8_t *lo_reg)
{
    if (channel == 0) {
        *hi_reg = FAN1_TACH_HI;
        *lo_reg = FAN1_TACH_LO;
    } else {
        *hi_reg = FAN2_TACH_HI;
        *lo_reg = FAN2_TACH_LO;
    }
}

esp_err_t fan_init(const fan_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    ESP_LOGI(TAG, "fan init addr=0x%02X port=%d", config->address, config->port);
    return ESP_OK;
}

esp_err_t fan_set_speed(uint8_t channel, uint8_t percent)
{
    if (channel > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        percent = 100;
    }

    uint8_t val = (uint8_t)(((uint16_t)percent * 255) / 100);
    uint8_t buf[2] = { fan_setting_reg(channel), val };

    ESP_LOGI(TAG, "fan%d speed %u%% -> 0x%02X", channel, percent, val);
    return i2c_master_write_to_device(s_config.port, s_config.address,
                                      buf, sizeof(buf),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

esp_err_t fan_get_rpm(uint8_t channel, uint16_t *rpm)
{
    if (channel > 1 || !rpm) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t hi_reg, lo_reg;
    fan_tach_regs(channel, &hi_reg, &lo_reg);

    uint8_t hi = 0, lo = 0;
    esp_err_t err;

    err = i2c_master_write_read_device(s_config.port, s_config.address,
                                        &hi_reg, 1, &hi, 1,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_master_write_read_device(s_config.port, s_config.address,
                                        &lo_reg, 1, &lo, 1,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        return err;
    }

    uint16_t tach_count = ((uint16_t)hi << 8) | lo;
    if (tach_count == 0 || tach_count == 0xFFFF) {
        *rpm = 0;
        return ESP_OK;
    }

    *rpm = (uint16_t)(3932160UL / tach_count);
    return ESP_OK;
}
