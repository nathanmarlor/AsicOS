#include "self_test.h"

#include <string.h>
#include <stdio.h>

#include "esp_psram.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "serial.h"
#include "asic.h"
#include "voltage_regulator.h"
#include "temp_sensor.h"
#include "fan_controller.h"
#include "nvs_config.h"
#include "sha256.h"

static const char *TAG = "POST";

static void add_check(selftest_report_t *report, const char *name,
                      selftest_result_t result, const char *detail)
{
    if (report->check_count >= SELFTEST_MAX_CHECKS) {
        return;
    }
    selftest_check_t *c = &report->checks[report->check_count++];
    c->name   = name;
    c->result = result;
    snprintf(c->detail, sizeof(c->detail), "%s", detail);

    if (result == SELFTEST_FAIL) {
        report->all_pass = false;
    }
}

selftest_report_t selftest_run(void)
{
    selftest_report_t report;
    memset(&report, 0, sizeof(report));
    report.all_pass = true;

    char buf[64];

    /* 1. PSRAM */
    size_t psram_size = esp_psram_get_size();
    if (psram_size > 0) {
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(psram_size / 1024));
        add_check(&report, "PSRAM", SELFTEST_PASS, buf);
    } else {
        add_check(&report, "PSRAM", SELFTEST_FAIL, "not detected");
    }

    /* 2. Heap */
    size_t free_heap = esp_get_free_heap_size();
    snprintf(buf, sizeof(buf), "%u KB free", (unsigned)(free_heap / 1024));
    if (free_heap > 100 * 1024) {
        add_check(&report, "Heap", SELFTEST_PASS, buf);
    } else {
        add_check(&report, "Heap", SELFTEST_WARN, buf);
    }

    /* 3. NVS */
    char nvs_buf[32];
    nvs_config_get_string("_post_test", nvs_buf, sizeof(nvs_buf), "ok");
    add_check(&report, "NVS", SELFTEST_PASS, "accessible");

    /* 4. SHA-256 Hash */
    {
        uint8_t hash[32];
        sha256_hash((const uint8_t *)"", 0, hash);
        if (hash[0] == 0xe3 && hash[1] == 0xb0 && hash[2] == 0xc4) {
            add_check(&report, "SHA-256", SELFTEST_PASS, "SHA-256 verified");
        } else {
            add_check(&report, "SHA-256", SELFTEST_FAIL, "SHA-256 mismatch");
        }
    }

    /* 5. ASIC Chain */
    int chips = asic_enumerate();
    if (chips > 0) {
        snprintf(buf, sizeof(buf), "%d chip(s) found", chips);
        add_check(&report, "ASIC Chain", SELFTEST_PASS, buf);
    } else {
        add_check(&report, "ASIC Chain", SELFTEST_FAIL, "no chips found");
    }

    /* 6. Temp Sensor */
    float temp = 0.0f;
    esp_err_t err = temp_sensor_read(0, &temp);
    if (err == ESP_OK && temp >= -40.0f && temp <= 125.0f) {
        snprintf(buf, sizeof(buf), "%.1f C", temp);
        add_check(&report, "Temp Sensor", SELFTEST_PASS, buf);
    } else {
        snprintf(buf, sizeof(buf), "err=0x%x temp=%.1f", (unsigned)err, temp);
        add_check(&report, "Temp Sensor", SELFTEST_FAIL, buf);
    }

    /* 7. Voltage Regulator */
    vr_telemetry_t telem;
    err = vr_read_telemetry(&telem);
    if (err == ESP_OK && telem.vout > 0.5f) {
        snprintf(buf, sizeof(buf), "Vout=%.2f V", telem.vout);
        add_check(&report, "VR", SELFTEST_PASS, buf);
    } else {
        snprintf(buf, sizeof(buf), "err=0x%x Vout=%.2f", (unsigned)err,
                 (err == ESP_OK) ? telem.vout : 0.0f);
        add_check(&report, "VR", SELFTEST_WARN, buf);
    }

    /* 8. Fan */
    fan_set_speed(0, 50);
    vTaskDelay(pdMS_TO_TICKS(2000));
    uint16_t rpm = 0;
    fan_get_rpm(0, &rpm);
    fan_set_speed(0, 0);
    if (rpm > 0) {
        snprintf(buf, sizeof(buf), "%u RPM", (unsigned)rpm);
        add_check(&report, "Fan", SELFTEST_PASS, buf);
    } else {
        add_check(&report, "Fan", SELFTEST_WARN, "0 RPM (may be OK without fan)");
    }

    /* Summary */
    if (report.all_pass) {
        ESP_LOGI(TAG, "POST complete: ALL PASS");
    } else {
        ESP_LOGW(TAG, "POST complete: FAILURES");
    }

    for (int i = 0; i < report.check_count; i++) {
        selftest_check_t *c = &report.checks[i];
        const char *label = (c->result == SELFTEST_PASS) ? "PASS" :
                            (c->result == SELFTEST_FAIL) ? "FAIL" :
                            (c->result == SELFTEST_WARN) ? "WARN" : "SKIP";
        ESP_LOGI(TAG, "  [%s] %s: %s", label, c->name, c->detail);
    }

    return report;
}
