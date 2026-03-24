#include "api_metrics.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hashrate_task.h"
#include "mining_task.h"
#include "power_task.h"
#include "result_task.h"
#include "stratum_client.h"
#include "wifi_task.h"
#include "nvs_config.h"
#include "board.h"

static const char *TAG = "api_metrics";

#define METRICS_BUF_SIZE 12288

esp_err_t api_metrics_handler(httpd_req_t *req)
{
    char *buf = malloc(METRICS_BUF_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate metrics buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int off = 0;
    const hashrate_info_t *hr = hashrate_task_get_info();
    const power_status_t *pw = power_task_get_status();
    const mining_stats_t *stats = result_task_get_stats();
    const board_config_t *board = board_get_config();

    uint32_t accepted = stratum_client_get_accepted();
    uint32_t rejected = stratum_client_get_rejected();
    double pool_diff = stratum_client_get_current_difficulty();
    stratum_state_t state = stratum_client_get_state();

    uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, DEFAULT_ASIC_FREQ);
    uint16_t voltage = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, DEFAULT_ASIC_VOLTAGE);

    int64_t uptime_us = esp_timer_get_time();

    /* Total hashrate */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_hashrate_ghs Total hashrate in GH/s\n"
        "# TYPE asicos_hashrate_ghs gauge\n"
        "asicos_hashrate_ghs %.1f\n\n",
        hr ? hr->total_hashrate_ghs : 0.0f);

    /* Expected hashrate */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_hashrate_expected_ghs Expected hashrate in GH/s\n"
        "# TYPE asicos_hashrate_expected_ghs gauge\n"
        "asicos_hashrate_expected_ghs %.1f\n\n",
        (float)freq * board->expected_chip_count * board->small_core_count / 1000.0f);

    /* Per-chip hashrate */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_chip_hashrate_ghs Per-chip hashrate in GH/s\n"
        "# TYPE asicos_chip_hashrate_ghs gauge\n");
    if (hr) {
        for (int i = 0; i < hr->chip_count; i++) {
            off += snprintf(buf + off, METRICS_BUF_SIZE - off,
                "asicos_chip_hashrate_ghs{chip=\"%d\"} %.1f\n",
                i, hr->per_chip_hashrate_ghs[i]);
        }
    }
    off += snprintf(buf + off, METRICS_BUF_SIZE - off, "\n");

    /* Per-domain hashrate */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_domain_hashrate_ghs Per-chip per-domain hashrate in GH/s\n"
        "# TYPE asicos_domain_hashrate_ghs gauge\n");
    if (hr) {
        for (int i = 0; i < hr->chip_count; i++) {
            for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
                off += snprintf(buf + off, METRICS_BUF_SIZE - off,
                    "asicos_domain_hashrate_ghs{chip=\"%d\",domain=\"%d\"} %.1f\n",
                    i, d, hr->per_domain_hashrate_ghs[i][d]);
            }
        }
    }
    off += snprintf(buf + off, METRICS_BUF_SIZE - off, "\n");

    /* Temperatures */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_chip_temp_celsius ASIC chip temperature\n"
        "# TYPE asicos_chip_temp_celsius gauge\n"
        "asicos_chip_temp_celsius %.1f\n\n"
        "# HELP asicos_vr_temp_celsius Voltage regulator temperature\n"
        "# TYPE asicos_vr_temp_celsius gauge\n"
        "asicos_vr_temp_celsius %.1f\n\n"
        "# HELP asicos_board_temp_celsius Board temperature\n"
        "# TYPE asicos_board_temp_celsius gauge\n"
        "asicos_board_temp_celsius %.1f\n\n",
        pw ? pw->chip_temp : 0.0f,
        pw ? pw->vr_temp : 0.0f,
        pw ? pw->board_temp : 0.0f);

    /* Power */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_power_watts Output power consumption in watts\n"
        "# TYPE asicos_power_watts gauge\n"
        "asicos_power_watts %.1f\n\n"
        "# HELP asicos_input_watts Input power in watts\n"
        "# TYPE asicos_input_watts gauge\n"
        "asicos_input_watts %.1f\n\n"
        "# HELP asicos_input_voltage_volts Input voltage\n"
        "# TYPE asicos_input_voltage_volts gauge\n"
        "asicos_input_voltage_volts %.3f\n\n"
        "# HELP asicos_input_current_amps Input current\n"
        "# TYPE asicos_input_current_amps gauge\n"
        "asicos_input_current_amps %.3f\n\n"
        "# HELP asicos_vr_voltage_volts VR output voltage\n"
        "# TYPE asicos_vr_voltage_volts gauge\n"
        "asicos_vr_voltage_volts %.4f\n\n"
        "# HELP asicos_vr_current_amps VR output current\n"
        "# TYPE asicos_vr_current_amps gauge\n"
        "asicos_vr_current_amps %.1f\n\n",
        pw ? pw->power_w : 0.0f,
        pw ? pw->input_w : 0.0f,
        pw ? pw->vin : 0.0f,
        pw ? pw->iin : 0.0f,
        pw ? pw->vout : 0.0f,
        pw ? pw->iout : 0.0f);

    /* Voltage & frequency */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_voltage_mv Core voltage in millivolts\n"
        "# TYPE asicos_voltage_mv gauge\n"
        "asicos_voltage_mv %u\n\n"
        "# HELP asicos_frequency_mhz ASIC frequency\n"
        "# TYPE asicos_frequency_mhz gauge\n"
        "asicos_frequency_mhz %u\n\n",
        (unsigned)voltage, (unsigned)freq);

    /* Shares */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_shares_submitted_total Total shares submitted\n"
        "# TYPE asicos_shares_submitted_total counter\n"
        "asicos_shares_submitted_total %llu\n\n"
        "# HELP asicos_shares_accepted_total Accepted shares\n"
        "# TYPE asicos_shares_accepted_total counter\n"
        "asicos_shares_accepted_total %u\n\n"
        "# HELP asicos_shares_rejected_total Rejected shares\n"
        "# TYPE asicos_shares_rejected_total counter\n"
        "asicos_shares_rejected_total %u\n\n"
        "# HELP asicos_duplicate_nonces_total Duplicate nonces detected\n"
        "# TYPE asicos_duplicate_nonces_total counter\n"
        "asicos_duplicate_nonces_total %llu\n\n",
        stats ? (unsigned long long)stats->total_shares_submitted : 0ULL,
        (unsigned)accepted,
        (unsigned)rejected,
        stats ? (unsigned long long)stats->duplicate_nonces : 0ULL);

    /* Error rate (rejected / (accepted + rejected)) */
    float error_pct = 0.0f;
    if (accepted + rejected > 0) {
        error_pct = (float)rejected / (float)(accepted + rejected) * 100.0f;
    }
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_error_percentage Share error/rejection rate\n"
        "# TYPE asicos_error_percentage gauge\n"
        "asicos_error_percentage %.2f\n\n",
        error_pct);

    /* Difficulty */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_session_best_difficulty Best share difficulty this session\n"
        "# TYPE asicos_session_best_difficulty gauge\n"
        "asicos_session_best_difficulty %.6e\n\n"
        "# HELP asicos_alltime_best_difficulty Best share difficulty ever\n"
        "# TYPE asicos_alltime_best_difficulty gauge\n"
        "asicos_alltime_best_difficulty %.6e\n\n"
        "# HELP asicos_pool_difficulty Current pool difficulty\n"
        "# TYPE asicos_pool_difficulty gauge\n"
        "asicos_pool_difficulty %.0f\n\n",
        stats ? stats->session_best_diff : 0.0,
        stats ? stats->alltime_best_diff : 0.0,
        pool_diff);

    /* Fans */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_fan_rpm Fan speed in RPM\n"
        "# TYPE asicos_fan_rpm gauge\n"
        "asicos_fan_rpm{fan=\"0\"} %u\n"
        "asicos_fan_rpm{fan=\"1\"} %u\n\n"
        "# HELP asicos_fan_speed_percent Fan speed as percentage\n"
        "# TYPE asicos_fan_speed_percent gauge\n"
        "asicos_fan_speed_percent{fan=\"0\"} %u\n"
        "asicos_fan_speed_percent{fan=\"1\"} %u\n\n",
        pw ? (unsigned)pw->fan0_rpm : 0,
        pw ? (unsigned)pw->fan1_rpm : 0,
        pw ? (unsigned)pw->fan0_pct : 0,
        pw ? (unsigned)pw->fan1_pct : 0);

    /* Overheat & power fault */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_overheat_mode Overheat protection active (1=yes)\n"
        "# TYPE asicos_overheat_mode gauge\n"
        "asicos_overheat_mode %d\n\n"
        "# HELP asicos_power_fault VR power fault active (1=yes)\n"
        "# TYPE asicos_power_fault gauge\n"
        "asicos_power_fault %d\n\n",
        pw ? (int)pw->overheat : 0,
        pw ? (int)pw->vr_fault : 0);

    /* Stratum connection state (numeric enum) */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_stratum_state Stratum connection state (0=disconnected,1=connecting,2=subscribing,3=authorizing,4=configuring,5=mining,6=error)\n"
        "# TYPE asicos_stratum_state gauge\n"
        "asicos_stratum_state %d\n\n"
        "# HELP asicos_pool_connected Pool connection status (1=mining)\n"
        "# TYPE asicos_pool_connected gauge\n"
        "asicos_pool_connected %d\n\n",
        (int)state,
        (state == STRATUM_STATE_MINING) ? 1 : 0);

    /* WiFi RSSI */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_wifi_rssi_dbm WiFi signal strength in dBm\n"
        "# TYPE asicos_wifi_rssi_dbm gauge\n"
        "asicos_wifi_rssi_dbm %d\n\n",
        (int)wifi_get_rssi());

    /* Uptime */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_uptime_seconds Device uptime\n"
        "# TYPE asicos_uptime_seconds counter\n"
        "asicos_uptime_seconds %lld\n\n",
        (long long)(uptime_us / 1000000));

    /* Heap breakdown */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_free_heap_bytes Free heap memory\n"
        "# TYPE asicos_free_heap_bytes gauge\n"
        "asicos_free_heap_bytes{type=\"total\"} %u\n"
        "asicos_free_heap_bytes{type=\"internal\"} %u\n"
        "asicos_free_heap_bytes{type=\"spiram\"} %u\n\n"
        "# HELP asicos_min_free_heap_bytes Minimum free heap since boot\n"
        "# TYPE asicos_min_free_heap_bytes gauge\n"
        "asicos_min_free_heap_bytes %u\n\n",
        (unsigned)esp_get_free_heap_size(),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
        (unsigned)esp_get_minimum_free_heap_size());

    /* Efficiency (J/TH) */
    float jth = 0.0f;
    if (hr && pw && hr->total_hashrate_ghs > 0.0f && pw->power_w > 0.0f) {
        jth = pw->power_w / (hr->total_hashrate_ghs / 1000.0f);
    }
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_efficiency_jth Mining efficiency in J/TH\n"
        "# TYPE asicos_efficiency_jth gauge\n"
        "asicos_efficiency_jth %.1f\n\n",
        jth);

    /* Stratum RTT */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_stratum_rtt_ms Stratum round-trip time (EMA) in ms\n"
        "# TYPE asicos_stratum_rtt_ms gauge\n"
        "asicos_stratum_rtt_ms %.1f\n\n",
        stratum_client_get_rtt_ms());

    /* Block height & new blocks */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_block_height Current Bitcoin block height\n"
        "# TYPE asicos_block_height gauge\n"
        "asicos_block_height %u\n\n"
        "# HELP asicos_blocks_found_total New blocks received from pool (clean_jobs)\n"
        "# TYPE asicos_blocks_found_total counter\n"
        "asicos_blocks_found_total %u\n\n",
        (unsigned)mining_get_block_height(),
        (unsigned)stratum_client_get_block_count());

    /* Rejected shares by reason */
    const stratum_rejection_reasons_t *reasons = stratum_client_get_rejection_reasons();
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_shares_rejected_reason Rejected shares by reason\n"
        "# TYPE asicos_shares_rejected_reason counter\n"
        "asicos_shares_rejected_reason{reason=\"job_not_found\"} %u\n"
        "asicos_shares_rejected_reason{reason=\"duplicate\"} %u\n"
        "asicos_shares_rejected_reason{reason=\"low_difficulty\"} %u\n"
        "asicos_shares_rejected_reason{reason=\"stale\"} %u\n"
        "asicos_shares_rejected_reason{reason=\"other\"} %u\n\n",
        (unsigned)reasons->job_not_found,
        (unsigned)reasons->duplicate,
        (unsigned)reasons->low_difficulty,
        (unsigned)reasons->stale,
        (unsigned)reasons->other);

    /* Per-chip HW errors */
    if (hr) {
        off += snprintf(buf + off, METRICS_BUF_SIZE - off,
            "# HELP asicos_chip_hw_errors_total Per-chip hardware errors\n"
            "# TYPE asicos_chip_hw_errors_total counter\n");
        for (int i = 0; i < hr->chip_count; i++) {
            off += snprintf(buf + off, METRICS_BUF_SIZE - off,
                "asicos_chip_hw_errors_total{chip=\"%d\"} %llu\n",
                i, (unsigned long long)result_task_get_chip_hw_errors(i));
        }
        off += snprintf(buf + off, METRICS_BUF_SIZE - off, "\n");
    }

#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
    /* CPU usage per task */
    {
        TaskStatus_t *task_array;
        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        task_array = malloc(task_count * sizeof(TaskStatus_t));
        if (task_array) {
            uint32_t total_runtime;
            task_count = uxTaskGetSystemState(task_array, task_count, &total_runtime);
            if (total_runtime > 0) {
                off += snprintf(buf + off, METRICS_BUF_SIZE - off,
                    "# HELP asicos_task_cpu_percent Per-task CPU usage\n"
                    "# TYPE asicos_task_cpu_percent gauge\n");
                for (UBaseType_t i = 0; i < task_count && off < METRICS_BUF_SIZE - 100; i++) {
                    float pct = (float)task_array[i].ulRunTimeCounter / (float)total_runtime * 100.0f;
                    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
                        "asicos_task_cpu_percent{task=\"%s\"} %.1f\n",
                        task_array[i].pcTaskName, pct);
                }
                off += snprintf(buf + off, METRICS_BUF_SIZE - off, "\n");
            }
            free(task_array);
        }
    }
#endif

    httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, off);

    free(buf);
    return ESP_OK;
}
