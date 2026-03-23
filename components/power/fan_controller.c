#include "fan_controller.h"
#include "emc2101.h"
#include "i2c_mux.h"
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
static emc2101_config_t s_emc2101[2];

/* ── EMC2302 helpers ──────────────────────────────────────────────── */

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

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t fan_init(const fan_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    ESP_LOGI(TAG, "fan init type=%d addr=0x%02X port=%d", config->type, config->address, config->port);

    if (config->type == FAN_TYPE_EMC2101_MUX) {
        /* Initialize both EMC2101 channels behind the mux */
        for (int i = 0; i < 2; i++) {
            s_emc2101[i].port = config->port;
            s_emc2101[i].mux_addr = config->address;
            s_emc2101[i].mux_channel = config->mux_channels[i];
            s_emc2101[i].dev_addr = config->dev_addr;

            esp_err_t err = emc2101_init(&s_emc2101[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "EMC2101 fan %d init failed: %s", i, esp_err_to_name(err));
                return err;
            }
        }
    }

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

    if (s_config.type == FAN_TYPE_EMC2101_MUX) {
        ESP_LOGD(TAG, "fan%d (EMC2101) speed %u%%", channel, percent);
        return emc2101_set_fan_duty(&s_emc2101[channel], percent);
    }

    /* EMC2302 path */
    uint8_t val = (uint8_t)(((uint16_t)percent * 255) / 100);
    uint8_t buf[2] = { fan_setting_reg(channel), val };

    ESP_LOGD(TAG, "fan%d speed %u%% -> 0x%02X", channel, percent, val);
    esp_err_t err = i2c_master_write_to_device(s_config.port, s_config.address,
                                                buf, sizeof(buf),
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "fan%d set speed failed: 0x%X", channel, err);
    }
    return err;
}

esp_err_t fan_get_rpm(uint8_t channel, uint16_t *rpm)
{
    if (channel > 1 || !rpm) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_config.type == FAN_TYPE_EMC2101_MUX) {
        return emc2101_get_fan_rpm(&s_emc2101[channel], rpm);
    }

    /* EMC2302 path */
    uint8_t hi_reg, lo_reg;
    fan_tach_regs(channel, &hi_reg, &lo_reg);

    uint8_t hi = 0, lo = 0;
    esp_err_t err;

    err = i2c_master_write_read_device(s_config.port, s_config.address,
                                        &hi_reg, 1, &hi, 1,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "fan%d tach read failed: 0x%X", channel, err);
        *rpm = 0;
        return err;
    }

    err = i2c_master_write_read_device(s_config.port, s_config.address,
                                        &lo_reg, 1, &lo, 1,
                                        pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "fan%d tach read failed: 0x%X", channel, err);
        *rpm = 0;
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
