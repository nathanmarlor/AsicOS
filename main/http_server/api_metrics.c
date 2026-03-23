#include "api_metrics.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "hashrate_task.h"
#include "power_task.h"
#include "result_task.h"
#include "stratum_client.h"
#include "nvs_config.h"
#include "board.h"

static const char *TAG = "api_metrics";

#define METRICS_BUF_SIZE 4096

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
    uint32_t free_heap = esp_get_free_heap_size();

    /* Total hashrate */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_hashrate_ghs Total hashrate in GH/s\n"
        "# TYPE asicos_hashrate_ghs gauge\n"
        "asicos_hashrate_ghs %.1f\n\n",
        hr ? hr->total_hashrate_ghs : 0.0f);

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

    /* Temperatures */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_chip_temp_celsius Chip temperature\n"
        "# TYPE asicos_chip_temp_celsius gauge\n"
        "asicos_chip_temp_celsius %.1f\n\n"
        "# HELP asicos_vr_temp_celsius VR temperature\n"
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
        "# HELP asicos_power_watts Power consumption\n"
        "# TYPE asicos_power_watts gauge\n"
        "asicos_power_watts %.1f\n\n"
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
        "asicos_shares_rejected_total %u\n\n",
        stats ? (unsigned long long)stats->total_shares_submitted : 0ULL,
        (unsigned)accepted,
        (unsigned)rejected);

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
        "# HELP asicos_fan_rpm Fan speed\n"
        "# TYPE asicos_fan_rpm gauge\n"
        "asicos_fan_rpm{fan=\"0\"} %u\n"
        "asicos_fan_rpm{fan=\"1\"} %u\n\n",
        pw ? (unsigned)pw->fan0_rpm : 0,
        pw ? (unsigned)pw->fan1_rpm : 0);

    /* Uptime */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_uptime_seconds Device uptime\n"
        "# TYPE asicos_uptime_seconds counter\n"
        "asicos_uptime_seconds %lld\n\n",
        (long long)(uptime_us / 1000000));

    /* Free heap */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_free_heap_bytes Free heap memory\n"
        "# TYPE asicos_free_heap_bytes gauge\n"
        "asicos_free_heap_bytes %u\n\n",
        (unsigned)free_heap);

    /* Pool connected */
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_pool_connected Pool connection status (1=connected)\n"
        "# TYPE asicos_pool_connected gauge\n"
        "asicos_pool_connected %d\n\n",
        (state == STRATUM_STATE_MINING) ? 1 : 0);

    /* Efficiency */
    float efficiency = 0.0f;
    if (hr && pw && pw->power_w > 0.0f) {
        efficiency = hr->total_hashrate_ghs / pw->power_w;
    }
    off += snprintf(buf + off, METRICS_BUF_SIZE - off,
        "# HELP asicos_efficiency_ghs_per_w Mining efficiency\n"
        "# TYPE asicos_efficiency_ghs_per_w gauge\n"
        "asicos_efficiency_ghs_per_w %.1f\n",
        efficiency);

    httpd_resp_set_type(req, "text/plain; version=0.0.4; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, buf, off);

    free(buf);
    return ESP_OK;
}
