#include "api_remote.h"

#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "licence.h"
#include "nvs_config.h"
#include "remote_access.h"

static const char *TAG = "api_remote";

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

/* ── GET /api/remote/status ────────────────────────────────────────── */

esp_err_t api_remote_status_handler(httpd_req_t *req)
{
    (void)TAG;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "state",    (double)remote_get_state());
    cJSON_AddBoolToObject(root, "licensed",   remote_is_licensed());
    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));
    cJSON_AddStringToObject(root, "device_id", device_id);

    send_json(req, root);
    return ESP_OK;
}

/* ── POST /api/remote/activate ─────────────────────────────────────── */

esp_err_t api_remote_activate_handler(httpd_req_t *req)
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

    cJSON *key_item = cJSON_GetObjectItem(json, "licence_key");
    if (!key_item || !cJSON_IsString(key_item)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing licence_key");
        return ESP_FAIL;
    }

    /* Save to NVS */
    nvs_config_set_string(NVS_KEY_LICENCE, key_item->valuestring);

    /* Validate immediately */
    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));
    bool valid = licence_validate(device_id, key_item->valuestring);
    cJSON_Delete(json);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "valid", valid);
    cJSON_AddStringToObject(root, "status", valid ? "activated" : "invalid_key");

    send_json(req, root);
    return ESP_OK;
}
