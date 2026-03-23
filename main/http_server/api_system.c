#include "api_system.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "hashrate_task.h"
#include "nvs_config.h"
#include "power_task.h"
#include "result_task.h"
#include "stratum_client.h"

static const char *TAG = "api_system";

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

static char *read_body(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 4096) {
        return NULL;
    }
    char *buf = calloc(1, total + 1);
    if (!buf) return NULL;

    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += ret;
    }
    return buf;
}

static const char *stratum_state_str(stratum_state_t s)
{
    switch (s) {
        case STRATUM_STATE_DISCONNECTED: return "disconnected";
        case STRATUM_STATE_CONNECTING:   return "connecting";
        case STRATUM_STATE_SUBSCRIBING:  return "subscribing";
        case STRATUM_STATE_AUTHORIZING:  return "authorizing";
        case STRATUM_STATE_CONFIGURING:  return "configuring";
        case STRATUM_STATE_MINING:       return "mining";
        case STRATUM_STATE_ERROR:        return "error";
        default:                         return "unknown";
    }
}

/* ── GET /api/system/info ──────────────────────────────────────────── */

esp_err_t api_system_info_handler(httpd_req_t *req)
{
    const board_config_t  *board = board_get_config();
    const hashrate_info_t *hr    = hashrate_task_get_info();
    const power_status_t  *pw    = power_task_get_status();
    const mining_stats_t  *stats = result_task_get_stats();

    cJSON *root = cJSON_CreateObject();

    /* Board info */
    cJSON_AddStringToObject(root, "board_name",  board->name);
    cJSON_AddStringToObject(root, "asic_model",  board->asic_model);
    cJSON_AddNumberToObject(root, "expected_chips", board->expected_chip_count);

    /* Hashrate */
    if (hr) {
        cJSON_AddNumberToObject(root, "hashrate_ghs", hr->total_hashrate_ghs);
        cJSON *chips = cJSON_AddArrayToObject(root, "per_chip_hashrate_ghs");
        for (int i = 0; i < hr->chip_count; i++) {
            cJSON_AddItemToArray(chips, cJSON_CreateNumber(hr->per_chip_hashrate_ghs[i]));
        }
        cJSON_AddNumberToObject(root, "chip_count", hr->chip_count);
    }

    /* Temps & power */
    if (pw) {
        cJSON *temps = cJSON_AddObjectToObject(root, "temps");
        cJSON_AddNumberToObject(temps, "chip",  pw->chip_temp);
        cJSON_AddNumberToObject(temps, "vr",    pw->vr_temp);
        cJSON_AddNumberToObject(temps, "board", pw->board_temp);

        cJSON *power = cJSON_AddObjectToObject(root, "power");
        cJSON_AddNumberToObject(power, "vin",     pw->vin);
        cJSON_AddNumberToObject(power, "vout",    pw->vout);
        cJSON_AddNumberToObject(power, "watts",   pw->power_w);
        cJSON_AddNumberToObject(power, "fan0_rpm", pw->fan0_rpm);
        cJSON_AddNumberToObject(power, "fan1_rpm", pw->fan1_rpm);
        cJSON_AddBoolToObject(power, "overheat",  pw->overheat);
        cJSON_AddBoolToObject(power, "vr_fault",  pw->vr_fault);
    }

    /* Mining stats */
    if (stats) {
        cJSON *mining = cJSON_AddObjectToObject(root, "mining");
        cJSON_AddNumberToObject(mining, "best_difficulty",        stats->best_difficulty);
        cJSON_AddNumberToObject(mining, "total_shares_submitted", (double)stats->total_shares_submitted);
        cJSON_AddNumberToObject(mining, "duplicate_nonces",       (double)stats->duplicate_nonces);
    }

    /* Pool / stratum */
    cJSON *pool = cJSON_AddObjectToObject(root, "pool");
    cJSON_AddStringToObject(pool, "state", stratum_state_str(stratum_client_get_state()));
    cJSON_AddNumberToObject(pool, "accepted",   stratum_client_get_accepted());
    cJSON_AddNumberToObject(pool, "rejected",   stratum_client_get_rejected());
    cJSON_AddNumberToObject(pool, "difficulty",  stratum_client_get_current_difficulty());

    /* Config from NVS */
    cJSON *config = cJSON_AddObjectToObject(root, "config");
    char buf[128];
    nvs_config_get_string(NVS_KEY_POOL_URL,  buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "pool_url", buf);
    cJSON_AddNumberToObject(config, "pool_port",
        nvs_config_get_u16(NVS_KEY_POOL_PORT, DEFAULT_POOL_PORT));
    nvs_config_get_string(NVS_KEY_POOL_USER, buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "pool_user", buf);
    cJSON_AddNumberToObject(config, "frequency",
        nvs_config_get_u16(NVS_KEY_ASIC_FREQ, DEFAULT_ASIC_FREQ));
    cJSON_AddNumberToObject(config, "voltage",
        nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, DEFAULT_ASIC_VOLTAGE));
    nvs_config_get_string(NVS_KEY_WIFI_SSID, buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "wifi_ssid", buf);
    nvs_config_get_string(NVS_KEY_UI_MODE, buf, sizeof(buf), "simple");
    cJSON_AddStringToObject(config, "ui_mode", buf);

    /* System */
    cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    send_json(req, root);
    return ESP_OK;
}

/* ── POST /api/system  (settings update) ──────────────────────────── */

esp_err_t api_system_patch_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }

    cJSON *json = cJSON_Parse(body);
    free(body);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *item;

    if ((item = cJSON_GetObjectItem(json, "pool_url")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_POOL_URL, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "pool_port")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_POOL_PORT, (uint16_t)item->valuedouble);

    if ((item = cJSON_GetObjectItem(json, "pool_user")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_POOL_USER, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "pool_pass")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_POOL_PASS, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "frequency")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_ASIC_FREQ, (uint16_t)item->valuedouble);

    if ((item = cJSON_GetObjectItem(json, "voltage")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, (uint16_t)item->valuedouble);

    if ((item = cJSON_GetObjectItem(json, "wifi_ssid")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_WIFI_SSID, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "wifi_pass")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_WIFI_PASS, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "ui_mode")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_UI_MODE, item->valuestring);

    cJSON_Delete(json);

    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ── POST /api/system/restart ──────────────────────────────────────── */

esp_err_t api_system_restart_handler(httpd_req_t *req)
{
    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"restarting\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK; /* unreachable */
}

/* ── POST /api/system/ota ──────────────────────────────────────────── */

esp_err_t api_system_ota_handler(httpd_req_t *req)
{
    set_cors(req);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    ESP_LOGI(TAG, "OTA: receiving %d bytes", remaining);

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, (remaining < 4096) ? remaining : 4096);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "OTA recv error");
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA complete, restarting...");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ota_complete\",\"restarting\":true}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK; /* unreachable */
}
