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

static const char *mode_str(tuner_mode_t m)
{
    switch (m) {
        case TUNER_MODE_ECO:      return "eco";
        case TUNER_MODE_BALANCED: return "balanced";
        case TUNER_MODE_POWER:    return "power";
        default:                  return "balanced";
    }
}

static tuner_mode_t parse_mode(const char *str)
{
    if (str && strcmp(str, "eco") == 0)   return TUNER_MODE_ECO;
    if (str && strcmp(str, "power") == 0) return TUNER_MODE_POWER;
    return TUNER_MODE_BALANCED;
}

/* ── GET /api/tuner/status ─────────────────────────────────────────── */

esp_err_t api_tuner_status_handler(httpd_req_t *req)
{
    (void)TAG;
    const tuner_status_t *st = tuner_get_status();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_str(st->state));
    cJSON_AddStringToObject(root, "mode", mode_str(st->mode));

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

    /* Parse optional mode from JSON body */
    tuner_mode_t mode = TUNER_MODE_BALANCED;
    int content_len = req->content_len;
    if (content_len > 0 && content_len < 256) {
        char buf[256] = {0};
        int ret = httpd_req_recv(req, buf, content_len);
        if (ret > 0) {
            cJSON *body = cJSON_Parse(buf);
            if (body) {
                cJSON *mode_json = cJSON_GetObjectItem(body, "mode");
                if (cJSON_IsString(mode_json)) {
                    mode = parse_mode(mode_json->valuestring);
                }
                cJSON_Delete(body);
            }
        }
    }

    tuner_start(mode,
                board->freq_min, board->freq_max,
                board->voltage_min, board->voltage_max);

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
