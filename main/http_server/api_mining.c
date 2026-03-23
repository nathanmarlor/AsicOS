#include "api_mining.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "hashrate_task.h"
#include "result_task.h"
#include "stratum_client.h"

static const char *TAG = "api_mining";

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

/* ── GET /api/mining/info ──────────────────────────────────────────── */

esp_err_t api_mining_info_handler(httpd_req_t *req)
{
    (void)TAG;
    const hashrate_info_t *hr    = hashrate_task_get_info();
    const mining_stats_t  *stats = result_task_get_stats();

    cJSON *root = cJSON_CreateObject();

    /* Hashrate */
    if (hr) {
        cJSON_AddNumberToObject(root, "hashrate_ghs", hr->total_hashrate_ghs);
        cJSON *chips = cJSON_AddArrayToObject(root, "chips");
        for (int i = 0; i < hr->chip_count; i++) {
            cJSON *chip = cJSON_CreateObject();
            cJSON_AddNumberToObject(chip, "id", i);
            cJSON_AddNumberToObject(chip, "hashrate_ghs", hr->per_chip_hashrate_ghs[i]);
            cJSON *domains = cJSON_AddArrayToObject(chip, "domains");
            for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
                cJSON_AddItemToArray(domains,
                    cJSON_CreateNumber(hr->per_domain_hashrate_ghs[i][d]));
            }
            cJSON_AddItemToArray(chips, chip);
        }
    }

    /* Shares -- field names match UI store interface */
    if (stats) {
        cJSON_AddNumberToObject(root, "best_diff",    stats->best_difficulty);
        cJSON_AddNumberToObject(root, "total_shares",  (double)stats->total_shares_submitted);
        cJSON_AddNumberToObject(root, "duplicates",    (double)stats->duplicate_nonces);
    }

    /* Total nonces returned by ASIC (for "all shares" display) */
    cJSON_AddNumberToObject(root, "total_nonces", (double)result_task_get_nonce_count());

    /* Pool */
    cJSON_AddNumberToObject(root, "pool_diff",  stratum_client_get_current_difficulty());
    cJSON_AddNumberToObject(root, "accepted",   stratum_client_get_accepted());
    cJSON_AddNumberToObject(root, "rejected",   stratum_client_get_rejected());

    send_json(req, root);
    return ESP_OK;
}
