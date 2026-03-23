#include "api_tuner.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "auto_tuner.h"
#include "board.h"
#include "tuner_task.h"

static const char *TAG = "api_tuner";

/* ── Helpers ───────────────────────────────────────────────────────── */

static void set_cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
}

static void send_json(httpd_req_t *req, cJSON *root)
{
    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    char *str = cJSON_PrintUnformatted(root);
    httpd_resp_sendstr(req, str);
    free(str);
    cJSON_Delete(root);
}

static const char *state_str(tuner_state_t s)
{
    switch (s) {
        case TUNER_STATE_IDLE:     return "idle";
        case TUNER_STATE_RUNNING:  return "running";
        case TUNER_STATE_COMPLETE: return "complete";
        case TUNER_STATE_ABORTED:  return "aborted";
        default:                   return "unknown";
    }
}

/* ── GET /api/tuner/status ─────────────────────────────────────────── */

esp_err_t api_tuner_status_handler(httpd_req_t *req)
{
    (void)TAG;
    const tuner_status_t *st = tuner_get_status();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_str(st->state));

    /* Progress */
    int pct = 0;
    if (st->total_steps > 0) {
        pct = (st->current_step * 100) / st->total_steps;
    }
    cJSON_AddNumberToObject(root, "progress_pct",  pct);
    cJSON_AddNumberToObject(root, "current_step",  st->current_step);
    cJSON_AddNumberToObject(root, "total_steps",   st->total_steps);

    /* Results array */
    cJSON *results = cJSON_AddArrayToObject(root, "results");
    for (int i = 0; i < st->result_count; i++) {
        const tuner_result_t *r = &st->results[i];
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddNumberToObject(entry, "freq",       r->freq);
        cJSON_AddNumberToObject(entry, "voltage",    r->voltage);
        cJSON_AddNumberToObject(entry, "hashrate",   r->hashrate_ghs);
        cJSON_AddNumberToObject(entry, "power",      r->power_w);
        cJSON_AddNumberToObject(entry, "temp",       r->temp);
        cJSON_AddNumberToObject(entry, "efficiency", r->efficiency_ghs_per_w);
        cJSON_AddBoolToObject(entry, "stable",       r->stable);
        cJSON_AddItemToArray(results, entry);
    }

    /* Best indices */
    cJSON_AddNumberToObject(root, "best_index",     st->best_index);
    cJSON_AddNumberToObject(root, "best_eff_index", st->best_eff_index);

    send_json(req, root);
    return ESP_OK;
}

/* ── POST /api/tuner/start ─────────────────────────────────────────── */

esp_err_t api_tuner_start_handler(httpd_req_t *req)
{
    const board_config_t *board = board_get_config();

    tuner_start(board->freq_min, board->freq_max, 25,
                board->voltage_min, board->voltage_max, 50);

    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"started\"}");
    return ESP_OK;
}

/* ── POST /api/tuner/abort ─────────────────────────────────────────── */

esp_err_t api_tuner_abort_handler(httpd_req_t *req)
{
    tuner_abort();

    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"aborted\"}");
    return ESP_OK;
}

/* ── POST /api/tuner/apply ─────────────────────────────────────────── */

esp_err_t api_tuner_apply_handler(httpd_req_t *req)
{
    tuner_apply_best();

    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"applied\"}");
    return ESP_OK;
}
