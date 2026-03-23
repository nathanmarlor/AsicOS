#include <string.h>
#include "voltage_regulator.h"
#include "esp_log.h"

static const char *TAG = "vr";

/* TPS53647 PMBus command codes */
#define PMBUS_OPERATION   0x01
#define PMBUS_VOUT_CMD    0x21
#define PMBUS_READ_VIN    0x88
#define PMBUS_READ_VOUT   0x8B
#define PMBUS_READ_IOUT   0x8C
#define PMBUS_READ_TEMP   0x8D
#define PMBUS_STATUS_BYTE 0x78

#define I2C_TIMEOUT_MS    100

static vr_config_t s_config;

/* ---- PMBus helpers ---- */

static esp_err_t pmbus_write8(uint8_t cmd, uint8_t value)
{
    uint8_t buf[2] = { cmd, value };
    return i2c_master_write_to_device(s_config.port, s_config.address,
                                      buf, sizeof(buf),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t pmbus_write16(uint8_t cmd, uint16_t value)
{
    uint8_t buf[3] = { cmd, (uint8_t)(value & 0xFF), (uint8_t)(value >> 8) };
    return i2c_master_write_to_device(s_config.port, s_config.address,
                                      buf, sizeof(buf),
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t pmbus_read16(uint8_t cmd, uint16_t *value)
{
    uint8_t rx[2] = {0};
    esp_err_t err = i2c_master_write_read_device(s_config.port, s_config.address,
                                                  &cmd, 1, rx, 2,
                                                  pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err == ESP_OK) {
        *value = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);
    }
    return err;
}

/* Convert PMBus LINEAR11 to float: 5-bit signed exponent + 11-bit signed mantissa */
static float linear11_to_float(uint16_t raw)
{
    int16_t exponent = (int16_t)(raw >> 11);
    int16_t mantissa = (int16_t)(raw & 0x7FF);

    /* Sign-extend exponent (5-bit) */
    if (exponent & 0x10) {
        exponent |= ~0x1F;
    }
    /* Sign-extend mantissa (11-bit) */
    if (mantissa & 0x400) {
        mantissa |= ~0x7FF;
    }

    float result = (float)mantissa;
    if (exponent >= 0) {
        for (int i = 0; i < exponent; i++) {
            result *= 2.0f;
        }
    } else {
        for (int i = 0; i < -exponent; i++) {
            result /= 2.0f;
        }
    }
    return result;
}

/* ---- Public API ---- */

esp_err_t vr_init(const vr_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;
    ESP_LOGI(TAG, "VR init addr=0x%02X port=%d", config->address, config->port);
    return ESP_OK;
}

esp_err_t vr_set_voltage(uint16_t millivolts)
{
    /* PMBus VOUT linear16, exponent = -9: val = (mV * 512) / 1000 */
    uint16_t val = (uint16_t)(((uint32_t)millivolts * 512) / 1000);
    ESP_LOGI(TAG, "set voltage %u mV -> 0x%04X", millivolts, val);
    return pmbus_write16(PMBUS_VOUT_CMD, val);
}

esp_err_t vr_enable(bool enable)
{
    /* ERRATA: OPERATION is a single-byte PMBus command */
    uint8_t val = enable ? 0x80 : 0x00;
    ESP_LOGI(TAG, "VR %s", enable ? "enable" : "disable");
    return pmbus_write8(PMBUS_OPERATION, val);
}

esp_err_t vr_read_telemetry(vr_telemetry_t *telemetry)
{
    if (!telemetry) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(telemetry, 0, sizeof(*telemetry));

    uint16_t raw;
    esp_err_t err;

    err = pmbus_read16(PMBUS_READ_VIN, &raw);
    if (err == ESP_OK) {
        telemetry->vin = linear11_to_float(raw);
    }

    err = pmbus_read16(PMBUS_READ_VOUT, &raw);
    if (err == ESP_OK) {
        /* VOUT uses linear16 with exponent -9 */
        telemetry->vout = (float)raw / 512.0f;
    }

    err = pmbus_read16(PMBUS_READ_IOUT, &raw);
    if (err == ESP_OK) {
        telemetry->iout = linear11_to_float(raw);
    }

    err = pmbus_read16(PMBUS_READ_TEMP, &raw);
    if (err == ESP_OK) {
        telemetry->temperature = linear11_to_float(raw);
    }

    return ESP_OK;
}

bool vr_check_faults(void)
{
    uint16_t raw = 0;
    esp_err_t err = pmbus_read16(PMBUS_STATUS_BYTE, &raw);
    if (err != ESP_OK) {
        /* I2C read failure - device may not support this command or wrong address.
         * Don't assume fault - return false to avoid emergency shutdown loops. */
        return false;
    }
    return (raw & 0xFF) != 0;
}
