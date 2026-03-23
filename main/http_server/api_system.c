#include "api_system.h"

#include <inttypes.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
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

#define GITHUB_OTA_REPO "nathanmarlor/AsicOS"

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
    cJSON_AddBoolToObject(root, "has_adc_vcore", board->has_adc_vcore);

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
        cJSON_AddNumberToObject(power, "iin",     pw->iin);
        cJSON_AddNumberToObject(power, "vout",    pw->vout);
        cJSON_AddNumberToObject(power, "iout",    pw->iout);
        cJSON_AddNumberToObject(power, "watts",   pw->power_w);
        cJSON_AddNumberToObject(power, "input_watts", pw->input_w);
        float eff_pct = (pw->input_w > 0.0f && pw->power_w > 0.0f)
                        ? (pw->power_w / pw->input_w) * 100.0f : 0.0f;
        cJSON_AddNumberToObject(power, "efficiency_pct", eff_pct);
        cJSON_AddNumberToObject(power, "fan0_rpm", pw->fan0_rpm);
        cJSON_AddNumberToObject(power, "fan1_rpm", pw->fan1_rpm);
        cJSON_AddNumberToObject(power, "fan0_pct", pw->fan0_pct);
        cJSON_AddNumberToObject(power, "fan1_pct", pw->fan1_pct);
        int fan_ovr = power_get_fan_override();
        cJSON_AddNumberToObject(power, "fan_override", fan_ovr);
        cJSON_AddStringToObject(power, "fan_mode", fan_ovr < 0 ? "auto" : "manual");
        cJSON_AddBoolToObject(power, "overheat",  pw->overheat);
        cJSON_AddBoolToObject(power, "vr_fault",  pw->vr_fault);
        cJSON_AddNumberToObject(power, "vr_temp",  pw->vr_temp);
        cJSON_AddNumberToObject(power, "vcore_adc_mv", pw->vcore_adc_mv);
    }

    /* Mining stats */
    if (stats) {
        cJSON *mining = cJSON_AddObjectToObject(root, "mining");
        cJSON_AddNumberToObject(mining, "session_best_diff",      stats->session_best_diff);
        cJSON_AddNumberToObject(mining, "alltime_best_diff",      stats->alltime_best_diff);
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
    nvs_config_get_string(NVS_KEY_POOL2_URL, buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "pool2_url", buf);
    cJSON_AddNumberToObject(config, "pool2_port",
        nvs_config_get_u16(NVS_KEY_POOL2_PORT, DEFAULT_POOL_PORT));
    nvs_config_get_string(NVS_KEY_POOL2_USER, buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "pool2_user", buf);
    cJSON_AddNumberToObject(config, "frequency",
        nvs_config_get_u16(NVS_KEY_ASIC_FREQ, DEFAULT_ASIC_FREQ));
    cJSON_AddNumberToObject(config, "voltage",
        nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, DEFAULT_ASIC_VOLTAGE));
    nvs_config_get_string(NVS_KEY_WIFI_SSID, buf, sizeof(buf), "");
    cJSON_AddStringToObject(config, "wifi_ssid", buf);
    nvs_config_get_string(NVS_KEY_UI_MODE, buf, sizeof(buf), "simple");
    cJSON_AddStringToObject(config, "ui_mode", buf);
    cJSON_AddNumberToObject(config, "freq_min",    board->freq_min);
    cJSON_AddNumberToObject(config, "freq_max",    board->freq_max);
    cJSON_AddNumberToObject(config, "voltage_min", board->voltage_min);
    cJSON_AddNumberToObject(config, "voltage_max", board->voltage_max);
    cJSON_AddNumberToObject(config, "fan_target",
        nvs_config_get_u16(NVS_KEY_FAN_TARGET_TEMP, board->fan_target_temp));
    cJSON_AddNumberToObject(config, "overheat_temp",
        nvs_config_get_u16(NVS_KEY_OVERHEAT_TEMP, board->overheat_temp));

    /* System */
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON_AddStringToObject(root, "firmware_version", app->version);
    cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    /* Reset reason */
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str = "unknown";
    switch (reason) {
        case ESP_RST_POWERON:  reason_str = "Power on"; break;
        case ESP_RST_SW:       reason_str = "Software restart"; break;
        case ESP_RST_PANIC:    reason_str = "Crash (panic)"; break;
        case ESP_RST_INT_WDT:  reason_str = "Watchdog (interrupt)"; break;
        case ESP_RST_TASK_WDT: reason_str = "Watchdog (task)"; break;
        case ESP_RST_WDT:      reason_str = "Watchdog"; break;
        case ESP_RST_DEEPSLEEP: reason_str = "Deep sleep"; break;
        case ESP_RST_BROWNOUT: reason_str = "Brownout"; break;
        case ESP_RST_SDIO:     reason_str = "SDIO"; break;
        case ESP_RST_USB:      reason_str = "USB"; break;
        default: break;
    }
    cJSON_AddStringToObject(root, "reset_reason", reason_str);

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

    if ((item = cJSON_GetObjectItem(json, "frequency")) && cJSON_IsNumber(item)) {
        const board_config_t *board = board_get_config();
        uint16_t freq = (uint16_t)item->valuedouble;
        if (freq >= board->freq_min && freq <= board->freq_max) {
            nvs_config_set_u16(NVS_KEY_ASIC_FREQ, freq);
        } else {
            ESP_LOGW(TAG, "Rejected frequency %" PRIu16 " (limits: %" PRIu16 "-%" PRIu16 ")",
                     freq, board->freq_min, board->freq_max);
        }
    }

    if ((item = cJSON_GetObjectItem(json, "voltage")) && cJSON_IsNumber(item)) {
        const board_config_t *board = board_get_config();
        uint16_t volt = (uint16_t)item->valuedouble;
        if (volt >= board->voltage_min && volt <= board->voltage_max) {
            nvs_config_set_u16(NVS_KEY_ASIC_VOLTAGE, volt);
        } else {
            ESP_LOGW(TAG, "Rejected voltage %" PRIu16 " (limits: %" PRIu16 "-%" PRIu16 ")",
                     volt, board->voltage_min, board->voltage_max);
        }
    }

    if ((item = cJSON_GetObjectItem(json, "wifi_ssid")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_WIFI_SSID, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "wifi_pass")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_WIFI_PASS, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "ui_mode")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_UI_MODE, item->valuestring);

    /* Fallback pool */
    if ((item = cJSON_GetObjectItem(json, "pool2_url")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_POOL2_URL, item->valuestring);

    if ((item = cJSON_GetObjectItem(json, "pool2_port")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_POOL2_PORT, (uint16_t)item->valuedouble);

    if ((item = cJSON_GetObjectItem(json, "pool2_user")) && cJSON_IsString(item))
        nvs_config_set_string(NVS_KEY_POOL2_USER, item->valuestring);

    /* Thermal settings */
    if ((item = cJSON_GetObjectItem(json, "fan_target")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_FAN_TARGET_TEMP, (uint16_t)item->valuedouble);

    if ((item = cJSON_GetObjectItem(json, "overheat_temp")) && cJSON_IsNumber(item))
        nvs_config_set_u16(NVS_KEY_OVERHEAT_TEMP, (uint16_t)item->valuedouble);

    /* Fan override: -1 = auto, 0-100 = manual % */
    if ((item = cJSON_GetObjectItem(json, "fan_override")) && cJSON_IsNumber(item))
        power_set_fan_override((int)item->valuedouble);

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

/* ── Helper: board slug for firmware filename ─────────────────────── */

static const char *board_slug(void)
{
    const board_config_t *board = board_get_config();
    /* Map board name to the slug used in GitHub release assets */
    if (strstr(board->name, "Nano"))    return "bitforge-nano";
    if (strstr(board->name, "NerdQ"))   return "nerdqaxepp";
    return "unknown";
}

/* ── GET /api/system/ota/check ────────────────────────────────────── */

/* Buffer for GitHub API JSON response */
#define OTA_CHECK_BUF_SIZE  4096

typedef struct {
    char  *buf;
    int    len;
    int    max;
} ota_resp_t;

static esp_err_t ota_check_http_event(esp_http_client_event_t *evt)
{
    ota_resp_t *ctx = (ota_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        int copy = evt->data_len;
        if (ctx->len + copy > ctx->max - 1) {
            copy = ctx->max - 1 - ctx->len;
        }
        if (copy > 0) {
            memcpy(ctx->buf + ctx->len, evt->data, copy);
            ctx->len += copy;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

esp_err_t api_system_ota_check_handler(httpd_req_t *req)
{
    set_cors(req);

    const esp_app_desc_t *app = esp_app_get_description();
    const char *slug = board_slug();

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.github.com/repos/%s/releases?per_page=1",
             GITHUB_OTA_REPO);

    char *resp_buf = calloc(1, OTA_CHECK_BUF_SIZE);
    if (!resp_buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    ota_resp_t ctx = { .buf = resp_buf, .len = 0, .max = OTA_CHECK_BUF_SIZE };

    esp_http_client_config_t config = {
        .url            = url,
        .event_handler  = ota_check_http_event,
        .user_data      = &ctx,
        .timeout_ms     = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");
    esp_http_client_set_header(client, "User-Agent", "AsicOS");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "GitHub API request failed: err=%s status=%d", esp_err_to_name(err), status);
        free(resp_buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "GitHub API request failed");
        return ESP_FAIL;
    }

    /* Parse the GitHub releases JSON (array with 1 element) */
    cJSON *gh_array = cJSON_Parse(resp_buf);
    free(resp_buf);
    if (!gh_array) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to parse GitHub response");
        return ESP_FAIL;
    }

    cJSON *gh = cJSON_IsArray(gh_array) ? cJSON_GetArrayItem(gh_array, 0) : gh_array;
    if (!gh) {
        cJSON_Delete(gh_array);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No releases found");
        return ESP_FAIL;
    }

    cJSON *tag = cJSON_GetObjectItem(gh, "tag_name");
    const char *latest_ver = (tag && cJSON_IsString(tag)) ? tag->valuestring : "unknown";

    /* Strip leading 'v' from tag if present */
    if (latest_ver[0] == 'v' || latest_ver[0] == 'V') {
        latest_ver++;
    }

    /* Build download URL for this board's firmware asset */
    char download_url[512];
    snprintf(download_url, sizeof(download_url),
             "https://github.com/%s/releases/latest/download/AsicOS-%s-fw.bin",
             GITHUB_OTA_REPO, slug);

    bool update_available = (strcmp(app->version, latest_ver) != 0);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "current_version", app->version);
    cJSON_AddStringToObject(root, "latest_version",  latest_ver);
    cJSON_AddBoolToObject(root, "update_available",  update_available);
    cJSON_AddStringToObject(root, "download_url",    download_url);

    cJSON_Delete(gh_array);
    send_json(req, root);
    return ESP_OK;
}

/* ── POST /api/system/ota/github ──────────────────────────────────── */

esp_err_t api_system_ota_github_handler(httpd_req_t *req)
{
    set_cors(req);

    /* Read optional body for download URL override, or build default */
    char download_url[512];
    char *body = read_body(req);
    bool url_from_body = false;

    if (body) {
        cJSON *json = cJSON_Parse(body);
        free(body);
        if (json) {
            cJSON *url_item = cJSON_GetObjectItem(json, "download_url");
            if (url_item && cJSON_IsString(url_item)) {
                strncpy(download_url, url_item->valuestring, sizeof(download_url) - 1);
                download_url[sizeof(download_url) - 1] = '\0';
                url_from_body = true;
            }
            cJSON_Delete(json);
        }
    }

    if (!url_from_body) {
        const char *slug = board_slug();
        snprintf(download_url, sizeof(download_url),
                 "https://github.com/%s/releases/latest/download/AsicOS-%s-fw.bin",
                 GITHUB_OTA_REPO, slug);
    }

    ESP_LOGI(TAG, "GitHub OTA: downloading from %s", download_url);

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

    esp_http_client_config_t config = {
        .url           = download_url,
        .timeout_ms    = 30000,
        .buffer_size   = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "AsicOS");

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Download failed");
        return ESP_FAIL;
    }

    int content_length = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "GitHub OTA: content_length=%d", content_length);

    char *buf = malloc(4096);
    if (!buf) {
        esp_http_client_cleanup(client);
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    int total_read = 0;
    while (1) {
        int read_len = esp_http_client_read(client, buf, 4096);
        if (read_len < 0) {
            ESP_LOGE(TAG, "HTTP read error");
            break;
        }
        if (read_len == 0) {
            /* Check if we're done or if it's a redirect */
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            continue;
        }

        err = esp_ota_write(ota_handle, buf, read_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            break;
        }
        total_read += read_len;
    }

    free(buf);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || total_read == 0) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA download/write failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "GitHub OTA: downloaded %d bytes", total_read);

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

    ESP_LOGI(TAG, "GitHub OTA complete, restarting...");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ota_complete\",\"restarting\":true}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();

    return ESP_OK; /* unreachable */
}
