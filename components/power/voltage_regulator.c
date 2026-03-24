#include <string.h>
#include <math.h>
#include "voltage_regulator.h"
#include "esp_log.h"

static const char *TAG = "vr";

/* ---- Common PMBus command codes ---- */
#define PMBUS_OPERATION       0x01
#define PMBUS_ON_OFF_CONFIG   0x02
#define PMBUS_CLEAR_FAULTS    0x03
#define PMBUS_VOUT_MODE       0x20
#define PMBUS_VOUT_CMD        0x21
#define PMBUS_VOUT_MAX        0x24
#define PMBUS_VOUT_MARGIN_HI  0x25
#define PMBUS_VOUT_MARGIN_LO  0x26
#define PMBUS_VOUT_SCALE_LOOP 0x29
#define PMBUS_FREQ_SWITCH     0x33
#define PMBUS_VIN_ON          0x35
#define PMBUS_VIN_OFF         0x36
#define PMBUS_VOUT_OV_FAULT_LIMIT   0x40
#define PMBUS_VOUT_OV_WARN_LIMIT    0x42
#define PMBUS_VOUT_UV_WARN_LIMIT    0x43
#define PMBUS_VOUT_UV_FAULT_LIMIT   0x44
#define PMBUS_IOUT_OC_FAULT   0x46
#define PMBUS_IOUT_OC_FAULT_RESPONSE 0x47
#define PMBUS_IOUT_OC_WARN    0x4A
#define PMBUS_OT_FAULT        0x4F
#define PMBUS_OT_FAULT_RESPONSE     0x50
#define PMBUS_OT_WARN         0x51
#define PMBUS_VIN_OV_FAULT    0x55
#define PMBUS_VIN_OV_FAULT_RESPONSE 0x56
#define PMBUS_VIN_UV_WARN_LIMIT     0x58
#define PMBUS_VOUT_MIN        0x2B
#define PMBUS_STATUS_WORD     0x79
#define PMBUS_STATUS_BYTE     0x78
#define PMBUS_READ_VIN        0x88
#define PMBUS_READ_VOUT       0x8B
#define PMBUS_READ_IOUT       0x8C
#define PMBUS_READ_TEMP       0x8D
#define PMBUS_PAGE            0x00

#define I2C_TIMEOUT_MS    100

/* TPS546D24A STATUS_WORD fault bits */
#define TPS546_FAULT_OFF      0x0040
#define TPS546_FAULT_VOUT_OV  0x0020
#define TPS546_FAULT_IOUT_OC  0x0010
#define TPS546_FAULT_VIN_UV   0x0008
#define TPS546_FAULT_TEMP     0x0004

static vr_config_t s_config;

/* VOUT_MODE exponent for TPS546D24A ULINEAR16 conversions */
static int8_t s_vout_exponent = -9;

/* ---- PMBus I2C helpers ---- */

static esp_err_t pmbus_write_cmd(uint8_t cmd)
{
    return i2c_master_write_to_device(s_config.port, s_config.address,
                                      &cmd, 1,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

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

static esp_err_t pmbus_read8(uint8_t cmd, uint8_t *value)
{
    return i2c_master_write_read_device(s_config.port, s_config.address,
                                        &cmd, 1, value, 1,
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

/* ---- Data format conversions ---- */

/* PMBus SLINEAR11: 5-bit signed exponent + 11-bit signed mantissa */
static float slinear11_to_float(uint16_t raw)
{
    int16_t exponent = (int16_t)(raw >> 11);
    if (exponent > 15) exponent -= 32;   /* sign extend 5-bit */
    int16_t mantissa = raw & 0x7FF;
    if (mantissa > 1023) mantissa -= 2048; /* sign extend 11-bit */
    return (float)mantissa * powf(2.0f, (float)exponent);
}

static uint16_t float_to_slinear11(float value)
{
    int exponent = 0;
    int mantissa = (int)value;

    /* Find exponent where mantissa fits in 11 bits */
    while (mantissa >= 1024 || mantissa < -1024) {
        mantissa >>= 1;
        exponent++;
    }

    if (value != 0 && mantissa == 0) {
        /* Small fractional value - use negative exponent */
        float scaled = value;
        exponent = 0;
        while (exponent > -15 && (int)scaled < 512) {
            scaled *= 2;
            exponent--;
        }
        mantissa = (int)scaled;
    }

    return ((uint16_t)(exponent & 0x1F) << 11) | (mantissa & 0x7FF);
}

/* PMBus ULINEAR16: unsigned 16-bit mantissa, exponent from VOUT_MODE register */
static float ulinear16_to_float(uint16_t raw)
{
    return (float)raw * powf(2.0f, (float)s_vout_exponent);
}

static uint16_t float_to_ulinear16(float value)
{
    return (uint16_t)(value / powf(2.0f, (float)s_vout_exponent));
}

/* ---- TPS53647 implementation ---- */

static esp_err_t tps53647_set_voltage(uint16_t millivolts)
{
    /* PMBus VOUT linear16, exponent = -9: val = (mV * 512) / 1000 */
    uint16_t val = (uint16_t)(((uint32_t)millivolts * 512) / 1000);
    ESP_LOGI(TAG, "TPS53647 set voltage %u mV -> 0x%04X", millivolts, val);
    return pmbus_write16(PMBUS_VOUT_CMD, val);
}

static esp_err_t tps53647_read_telemetry(vr_telemetry_t *telemetry)
{
    uint16_t raw;
    esp_err_t err;

    err = pmbus_read16(PMBUS_READ_VIN, &raw);
    if (err == ESP_OK) {
        telemetry->vin = slinear11_to_float(raw);
    }

    err = pmbus_read16(PMBUS_READ_VOUT, &raw);
    if (err == ESP_OK) {
        /* VOUT uses linear16 with exponent -9 */
        telemetry->vout = (float)raw / 512.0f;
    }

    err = pmbus_read16(PMBUS_READ_IOUT, &raw);
    if (err == ESP_OK) {
        telemetry->iout = slinear11_to_float(raw);
    }

    err = pmbus_read16(PMBUS_READ_TEMP, &raw);
    if (err == ESP_OK) {
        telemetry->temperature = slinear11_to_float(raw);
    }

    return ESP_OK;
}

static bool tps53647_check_faults(void)
{
    uint16_t raw = 0;
    esp_err_t err = pmbus_read16(PMBUS_STATUS_BYTE, &raw);
    if (err != ESP_OK) {
        /* I2C read failure - don't treat as fault */
        ESP_LOGW(TAG, "TPS53647 STATUS_BYTE read failed: %s", esp_err_to_name(err));
        return false;
    }
    return (raw & 0xFF) != 0;
}

/* ---- TPS546D24A implementation ---- */

static esp_err_t tps546_init(void)
{
    esp_err_t err;

    /* Step 1: Turn off output */
    err = pmbus_write8(PMBUS_OPERATION, 0x00);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TPS546 OPERATION write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Step 2: Set ON_OFF_CONFIG */
    err = pmbus_write8(PMBUS_ON_OFF_CONFIG, 0x1F);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TPS546 ON_OFF_CONFIG write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Step 3: Read VOUT_MODE to get exponent for ULINEAR16 conversions */
    uint8_t vout_mode = 0;
    err = pmbus_read8(PMBUS_VOUT_MODE, &vout_mode);
    if (err == ESP_OK) {
        /* Lower 5 bits are the exponent in two's complement */
        s_vout_exponent = (int8_t)(vout_mode & 0x1F);
        if (s_vout_exponent > 15) {
            s_vout_exponent -= 32;
        }
        ESP_LOGI(TAG, "TPS546 VOUT_MODE=0x%02X exponent=%d", vout_mode, s_vout_exponent);
    } else {
        ESP_LOGW(TAG, "TPS546 VOUT_MODE read failed, using default exponent=%d", s_vout_exponent);
    }

    /* Step 4: Write configuration parameters */
    /* FREQUENCY_SWITCH = 650 kHz */
    err = pmbus_write16(PMBUS_FREQ_SWITCH, float_to_slinear11(650.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 FREQ_SWITCH write failed: %s", esp_err_to_name(err));
    }

    /* VIN_ON = 11V */
    err = pmbus_write16(PMBUS_VIN_ON, float_to_slinear11(11.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VIN_ON write failed: %s", esp_err_to_name(err));
    }

    /* VIN_OFF = 10.5V */
    err = pmbus_write16(PMBUS_VIN_OFF, float_to_slinear11(10.5f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VIN_OFF write failed: %s", esp_err_to_name(err));
    }

    /* VIN_OV_FAULT = 13.5V */
    err = pmbus_write16(PMBUS_VIN_OV_FAULT, float_to_slinear11(13.5f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VIN_OV write failed: %s", esp_err_to_name(err));
    }

    /* VOUT_SCALE_LOOP = 0.25 */
    err = pmbus_write16(PMBUS_VOUT_SCALE_LOOP, float_to_slinear11(0.25f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_SCALE_LOOP write failed: %s", esp_err_to_name(err));
    }

    /* VOUT_MIN = 1.0V (ULINEAR16) */
    err = pmbus_write16(PMBUS_VOUT_MIN, float_to_ulinear16(1.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_MIN write failed: %s", esp_err_to_name(err));
    }

    /* VOUT_MAX = 2.0V (ULINEAR16) */
    err = pmbus_write16(PMBUS_VOUT_MAX, float_to_ulinear16(2.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_MAX write failed: %s", esp_err_to_name(err));
    }

    /* IOUT_OC_WARN = 38A */
    err = pmbus_write16(PMBUS_IOUT_OC_WARN, float_to_slinear11(38.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 IOUT_OC_WARN write failed: %s", esp_err_to_name(err));
    }

    /* IOUT_OC_FAULT = 40A */
    err = pmbus_write16(PMBUS_IOUT_OC_FAULT, float_to_slinear11(40.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 IOUT_OC_FAULT write failed: %s", esp_err_to_name(err));
    }

    /* OT_WARN = 105C */
    err = pmbus_write16(PMBUS_OT_WARN, float_to_slinear11(105.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 OT_WARN write failed: %s", esp_err_to_name(err));
    }

    /* OT_FAULT = 145C */
    err = pmbus_write16(PMBUS_OT_FAULT, float_to_slinear11(145.0f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 OT_FAULT write failed: %s", esp_err_to_name(err));
    }

    /* Step 4b: Fault response configuration (matching ForgeOS) */

    /* VIN_UV_WARN_LIMIT = 10.8V (SLINEAR11) */
    err = pmbus_write16(PMBUS_VIN_UV_WARN_LIMIT, float_to_slinear11(10.8f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VIN_UV_WARN_LIMIT write failed: %s", esp_err_to_name(err));
    }

    /* VIN_OV_FAULT_RESPONSE = 0xB7 (shutdown + 6 retries with hiccup) */
    err = pmbus_write8(PMBUS_VIN_OV_FAULT_RESPONSE, 0xB7);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VIN_OV_FAULT_RESPONSE write failed: %s", esp_err_to_name(err));
    }

    /* IOUT_OC_FAULT_RESPONSE = 0xC0 (immediate shutdown, no retries) */
    err = pmbus_write8(PMBUS_IOUT_OC_FAULT_RESPONSE, 0xC0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 IOUT_OC_FAULT_RESPONSE write failed: %s", esp_err_to_name(err));
    }

    /* OT_FAULT_RESPONSE = 0xFF (wait for cooling, retry indefinitely) */
    err = pmbus_write8(PMBUS_OT_FAULT_RESPONSE, 0xFF);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 OT_FAULT_RESPONSE write failed: %s", esp_err_to_name(err));
    }

    /* Step 5: Clear faults (send-byte command, no data) */
    err = pmbus_write_cmd(PMBUS_CLEAR_FAULTS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 CLEAR_FAULTS failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "TPS546D24A init complete");
    return ESP_OK;
}

static esp_err_t tps546_set_voltage(uint16_t millivolts)
{
    float voltage = (float)millivolts / 1000.0f;
    uint16_t val = float_to_ulinear16(voltage);
    ESP_LOGI(TAG, "TPS546 set voltage %u mV -> 0x%04X (exp=%d)", millivolts, val, s_vout_exponent);

    /* Set VOUT OV/UV protection limits relative to commanded voltage (ULINEAR16) */
    esp_err_t err;
    err = pmbus_write16(PMBUS_VOUT_OV_FAULT_LIMIT, float_to_ulinear16(voltage * 1.25f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_OV_FAULT_LIMIT write failed: %s", esp_err_to_name(err));
    }
    err = pmbus_write16(PMBUS_VOUT_OV_WARN_LIMIT, float_to_ulinear16(voltage * 1.15f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_OV_WARN_LIMIT write failed: %s", esp_err_to_name(err));
    }
    err = pmbus_write16(PMBUS_VOUT_UV_WARN_LIMIT, float_to_ulinear16(voltage * 0.85f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_UV_WARN_LIMIT write failed: %s", esp_err_to_name(err));
    }
    err = pmbus_write16(PMBUS_VOUT_UV_FAULT_LIMIT, float_to_ulinear16(voltage * 0.75f));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TPS546 VOUT_UV_FAULT_LIMIT write failed: %s", esp_err_to_name(err));
    }

    err = pmbus_write16(PMBUS_VOUT_CMD, val);
    if (err != ESP_OK) {
        return err;
    }

    /* Enable output after setting voltage */
    return pmbus_write8(PMBUS_OPERATION, 0x80);
}

static esp_err_t tps546_read_telemetry(vr_telemetry_t *telemetry)
{
    uint16_t raw;
    esp_err_t err;

    /* VIN: SLINEAR11 */
    err = pmbus_read16(PMBUS_READ_VIN, &raw);
    if (err == ESP_OK) {
        telemetry->vin = slinear11_to_float(raw);
    } else {
        ESP_LOGW(TAG, "TPS546 READ_VIN failed: %s", esp_err_to_name(err));
    }

    /* VOUT: ULINEAR16 */
    err = pmbus_read16(PMBUS_READ_VOUT, &raw);
    if (err == ESP_OK) {
        telemetry->vout = ulinear16_to_float(raw);
    } else {
        ESP_LOGW(TAG, "TPS546 READ_VOUT failed: %s", esp_err_to_name(err));
    }

    /* IOUT: SLINEAR11 - need to set PHASE=0xFF first for total current */
    uint8_t saved_page = 0;
    bool page_saved = false;
    err = pmbus_read8(PMBUS_PAGE, &saved_page);
    if (err == ESP_OK) {
        page_saved = true;
        pmbus_write8(PMBUS_PAGE, 0xFF);
    }

    err = pmbus_read16(PMBUS_READ_IOUT, &raw);
    if (err == ESP_OK) {
        telemetry->iout = slinear11_to_float(raw);
    } else {
        ESP_LOGW(TAG, "TPS546 READ_IOUT failed: %s", esp_err_to_name(err));
    }

    /* Restore PAGE register */
    if (page_saved) {
        pmbus_write8(PMBUS_PAGE, saved_page);
    }

    /* TEMP: SLINEAR11 */
    err = pmbus_read16(PMBUS_READ_TEMP, &raw);
    if (err == ESP_OK) {
        telemetry->temperature = slinear11_to_float(raw);
    } else {
        ESP_LOGW(TAG, "TPS546 READ_TEMP failed: %s", esp_err_to_name(err));
    }

    return ESP_OK;
}

static bool tps546_check_faults(void)
{
    uint16_t status = 0;
    esp_err_t err = pmbus_read16(PMBUS_STATUS_WORD, &status);
    if (err != ESP_OK) {
        /* I2C read failure - don't treat as fault */
        ESP_LOGW(TAG, "TPS546 STATUS_WORD read failed: %s", esp_err_to_name(err));
        return false;
    }

    uint16_t fault_mask = TPS546_FAULT_VOUT_OV | TPS546_FAULT_IOUT_OC |
                          TPS546_FAULT_VIN_UV  | TPS546_FAULT_TEMP;
    if (status & fault_mask) {
        ESP_LOGW(TAG, "TPS546 fault detected: STATUS_WORD=0x%04X%s%s%s%s",
                 status,
                 (status & TPS546_FAULT_VOUT_OV) ? " VOUT_OV" : "",
                 (status & TPS546_FAULT_IOUT_OC) ? " IOUT_OC" : "",
                 (status & TPS546_FAULT_VIN_UV)  ? " VIN_UV"  : "",
                 (status & TPS546_FAULT_TEMP)    ? " TEMP"    : "");
        return true;
    }

    return false;
}

/* ---- Public API (dispatches based on VR type) ---- */

esp_err_t vr_init(const vr_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    const char *type_name = (s_config.type == VR_TYPE_TPS546D24A) ? "TPS546D24A" : "TPS53647";
    ESP_LOGI(TAG, "VR init: %s addr=0x%02X port=%d", type_name, config->address, config->port);

    if (s_config.type == VR_TYPE_TPS546D24A) {
        return tps546_init();
    }

    /* TPS53647 doesn't need special initialization */
    return ESP_OK;
}

esp_err_t vr_set_voltage(uint16_t millivolts)
{
    if (s_config.type == VR_TYPE_TPS546D24A) {
        return tps546_set_voltage(millivolts);
    }
    return tps53647_set_voltage(millivolts);
}

esp_err_t vr_enable(bool enable)
{
    uint8_t val = enable ? 0x80 : 0x00;
    const char *type_name = (s_config.type == VR_TYPE_TPS546D24A) ? "TPS546" : "TPS53647";
    ESP_LOGI(TAG, "%s %s", type_name, enable ? "enable" : "disable");
    return pmbus_write8(PMBUS_OPERATION, val);
}

esp_err_t vr_read_telemetry(vr_telemetry_t *telemetry)
{
    if (!telemetry) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(telemetry, 0, sizeof(*telemetry));

    if (s_config.type == VR_TYPE_TPS546D24A) {
        return tps546_read_telemetry(telemetry);
    }
    return tps53647_read_telemetry(telemetry);
}

bool vr_check_faults(void)
{
    if (s_config.type == VR_TYPE_TPS546D24A) {
        return tps546_check_faults();
    }
    return tps53647_check_faults();
}
