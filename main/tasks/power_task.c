#include "power_task.h"

#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "adc_monitor.h"
#include "board.h"
#include "bm1370.h"
#include "fan_controller.h"
#include "ina260.h"
#include "led_status.h"
#include "nvs_config.h"
#include "pid.h"
#include "temp_sensor.h"
#include "voltage_regulator.h"

static const char *TAG = "power";

/* ── Poll interval ────────────────────────────────────────────────── */

#define POLL_INTERVAL_MS       2000
#define ASIC_TEMP_INTERVAL     15    /* read ASIC temp every 15 iterations (~30 s) */

/* ── PID gains ────────────────────────────────────────────────────── */

#define PID_KP   6.0f
#define PID_KI   0.1f
#define PID_KD  10.0f
#define PID_OUT_MIN  0.0f
#define PID_OUT_MAX  100.0f

/* ── Thermal limits ───────────────────────────────────────────────── */

#define VR_OVERHEAT_TEMP  90.0f

/* ── Static state ─────────────────────────────────────────────────── */

static volatile power_status_t s_status;
static pid_controller_t s_fan_pid[2];
static volatile int s_fan_override = -1;  // -1 = auto (PID), 0-100 = manual %
static ina260_config_t s_ina260_config;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void emergency_shutdown(void)
{
    ESP_LOGE(TAG, "Emergency shutdown: disabling voltage regulator");
    vr_enable(false);
    fan_set_speed(0, 100);
    fan_set_speed(1, 100);
}

/* ── Main task ────────────────────────────────────────────────────── */

static void power_task(void *pvParameters)
{
    (void)pvParameters;

    const board_config_t *board = board_get_config();
    const float dt = (float)POLL_INTERVAL_MS / 1000.0f;

    /* Initialise INA260 config if present */
    if (board->power_monitor_type == 1 && board->power_monitor_addr != 0) {
        s_ina260_config.port = I2C_NUM_0;
        s_ina260_config.address = board->power_monitor_addr;
    }

    /* Initialise PID controllers */
    pid_init(&s_fan_pid[0], PID_KP, PID_KI, PID_KD, PID_OUT_MIN, PID_OUT_MAX);
    pid_set_target(&s_fan_pid[0], (float)board->fan_target_temp);

    pid_init(&s_fan_pid[1], PID_KP, PID_KI, PID_KD, PID_OUT_MIN, PID_OUT_MAX);
    pid_set_target(&s_fan_pid[1], (float)board->vr_target_temp);

    uint16_t overheat_temp = board->overheat_temp;
    uint32_t iteration = 0;

    /* Restore fan override from NVS */
    uint16_t saved_fan = nvs_config_get_u16("fan_override", 0xFFFF);
    if (saved_fan != 0xFFFF && saved_fan <= 100) {
        s_fan_override = (int)saved_fan;
        ESP_LOGI(TAG, "Restored fan override from NVS: %d%%", s_fan_override);
    }

    ESP_LOGI(TAG, "Power task started: fan_target=%" PRIu16 " overheat=%" PRIu16 " vr_target=%" PRIu16,
             board->fan_target_temp, overheat_temp, board->vr_target_temp);

    while (1) {
        /* ── Read board temps (TMP1075) every cycle ──────────────── */
        float board_temp = 0.0f;
        if (temp_sensor_read(0, &board_temp) == ESP_OK) {
            s_status.board_temp = board_temp;
        }

        /* ── Chip temp: use EMC2101 external diode (same as board_temp) ─ */
        /* Reading the BM1370 temp register over UART conflicts with
         * job sends / nonce reads at 1 Mbaud, causing garbage results.
         * The EMC2101 external temperature channel reads the ASIC's
         * thermal diode via I2C instead, which is conflict-free.
         * On EMC2101-based boards sensor 0 IS the ASIC diode, so
         * chip_temp == board_temp.  Avoids a redundant I2C transaction. */
        if (board_temp > 0.0f) {
            s_status.chip_temp = board_temp;
        }

        /* ── Read VR telemetry ───────────────────────────────────── */
        vr_telemetry_t telem = {0};
        if (vr_read_telemetry(&telem) == ESP_OK) {
            s_status.vout    = telem.vout;
            s_status.iout    = telem.iout;
            s_status.vr_temp = telem.temperature;
            s_status.input_w = telem.vin * telem.iin;
            if (board->power_monitor_type == 0) {
                /* VR telemetry is the only power source */
                s_status.vin     = telem.vin;
                s_status.iin     = telem.iin;
                s_status.power_w = telem.pout;
            }
        }

        /* ── Read INA260 power monitor (if present) ──────────── */
        if (board->power_monitor_type == 1) {
            ina260_reading_t ina_reading = {0};
            if (ina260_read(&s_ina260_config, &ina_reading) == ESP_OK) {
                s_status.vin     = ina_reading.voltage_mv / 1000.0f;
                s_status.iin     = ina_reading.current_ma / 1000.0f;
                s_status.power_w = ina_reading.power_mw / 1000.0f;
            }
        }

        /* ── Read ADC (VCORE + thermistors) if board supports it ── */
        if (board->has_adc_vcore) {
            adc_readings_t adc = {0};
            if (adc_monitor_read(&adc) == ESP_OK) {
                s_status.vcore_adc_mv = adc.vcore_mv;
                s_status.therm1_temp = adc.therm1_temp_c;
                s_status.therm2_temp = adc.therm2_temp_c;

                /* ADC VCORE cross-check disabled: known ADC channel
                 * offset causes persistent false mismatch warnings. */
            }
        }

        /* ── Check VR faults ─────────────────────────────────────── */
        s_status.vr_fault = vr_check_faults();

        /* ── Overheat detection ──────────────────────────────────── */
        bool overheat = (s_status.chip_temp > (float)overheat_temp) ||
                        (s_status.vr_temp > VR_OVERHEAT_TEMP);
        s_status.overheat = overheat;

        if (overheat || s_status.vr_fault) {
            if (overheat) {
                ESP_LOGE(TAG, "OVERHEAT: chip=%.1fC vr=%.1fC (limits: chip=%" PRIu16 " vr=%.0f)",
                         s_status.chip_temp, s_status.vr_temp,
                         overheat_temp, VR_OVERHEAT_TEMP);
            }
            if (s_status.vr_fault) {
                ESP_LOGE(TAG, "VR FAULT detected");
            }
            led_set_state(LED_STATE_ERROR);
            emergency_shutdown();
        } else {
            /* ── Software PID fan control (EMC2302 or EMC2101_MUX) ── */
            if (s_fan_override >= 0) {
                fan_set_speed(0, (uint8_t)s_fan_override);
                fan_set_speed(1, (uint8_t)s_fan_override);
                s_status.fan0_pct = (uint8_t)s_fan_override;
                s_status.fan1_pct = (uint8_t)s_fan_override;
            } else {
                float fan0_pct = pid_compute(&s_fan_pid[0], s_status.chip_temp, dt);
                float fan1_pct = pid_compute(&s_fan_pid[1], s_status.vr_temp, dt);

                fan_set_speed(0, (uint8_t)fan0_pct);
                fan_set_speed(1, (uint8_t)fan1_pct);
                s_status.fan0_pct = (uint8_t)fan0_pct;
                s_status.fan1_pct = (uint8_t)fan1_pct;
            }
        }

        /* ── Read fan RPM ────────────────────────────────────────── */
        fan_get_rpm(0, &s_status.fan0_rpm);
        fan_get_rpm(1, &s_status.fan1_rpm);

        /* ── Debug log ───────────────────────────────────────────── */
        ESP_LOGD(TAG, "chip=%.1fC vr=%.1fC board=%.1fC therm1=%.1fC therm2=%.1fC | "
                 "vin=%.2fV vout=%.2fV vcore_adc=%.0fmV pwr=%.1fW | "
                 "fan0=%" PRIu16 " rpm fan1=%" PRIu16 " rpm | oh=%d fault=%d",
                 s_status.chip_temp, s_status.vr_temp, s_status.board_temp,
                 s_status.therm1_temp, s_status.therm2_temp,
                 s_status.vin, s_status.vout, s_status.vcore_adc_mv, s_status.power_w,
                 s_status.fan0_rpm, s_status.fan1_rpm,
                 s_status.overheat, s_status.vr_fault);

        iteration++;
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

void power_task_start(void)
{
    xTaskCreate(power_task, "power", POWER_TASK_STACK_SIZE, NULL,
                POWER_TASK_PRIORITY, NULL);
}

const power_status_t *power_task_get_status(void)
{
    return (const power_status_t *)&s_status;
}

void power_set_fan_override(int percent)
{
    if (percent < 0) {
        s_fan_override = -1;
        ESP_LOGI(TAG, "Fan override disabled, returning to PID auto");
        nvs_config_set_u16("fan_override", 0xFFFF);  /* 0xFFFF = auto */
    } else {
        s_fan_override = (percent > 100) ? 100 : percent;
        ESP_LOGI(TAG, "Fan override set to %d%%", s_fan_override);
        nvs_config_set_u16("fan_override", (uint16_t)s_fan_override);
    }
}

int power_get_fan_override(void)
{
    return s_fan_override;
}
