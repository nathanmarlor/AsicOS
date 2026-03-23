#include "stratum_client.h"
#include "stratum_transport.h"
#include "stratum_api.h"
#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "stratum_client";

#define MSG_BUF_SIZE  2048
#define LINE_BUF_SIZE 4096
#define VERSION_ROLLING_MASK 0x1FFFE000

/* Module state */
static stratum_client_config_t s_config;
static stratum_conn_t         *s_conn;
static stratum_state_t         s_state = STRATUM_STATE_DISCONNECTED;
static stratum_subscribe_result_t s_sub_result;
static double                  s_current_diff = 1.0;
static uint32_t                s_accepted;
static uint32_t                s_rejected;
static stratum_pool_config_t  *s_current_pool;

/* Track pending submit message IDs for accept/reject counting */
#define MAX_PENDING_IDS 64
static int s_pending_ids[MAX_PENDING_IDS];
static int s_pending_count;

static void add_pending_id(int id)
{
    if (s_pending_count < MAX_PENDING_IDS)
        s_pending_ids[s_pending_count++] = id;
}

static bool remove_pending_id(int id)
{
    for (int i = 0; i < s_pending_count; i++) {
        if (s_pending_ids[i] == id) {
            s_pending_ids[i] = s_pending_ids[--s_pending_count];
            return true;
        }
    }
    return false;
}

static int pool_id(void)
{
    return (s_current_pool == &s_config.primary) ? 0 : 1;
}

static void handle_line(const char *line)
{
    if (!line || line[0] == '\0') {
        return;
    }

    char method[64];
    if (stratum_detect_method(line, method, sizeof(method)) == 0) {
        /* Server push methods */
        if (strcmp(method, "mining.notify") == 0) {
            stratum_notify_t notify;
            if (stratum_parse_notify(line, &notify) == 0 && s_config.on_notify) {
                s_config.on_notify(&notify, pool_id());
            }
        } else if (strcmp(method, "mining.set_difficulty") == 0) {
            double diff;
            if (stratum_parse_set_difficulty(line, &diff) == 0) {
                s_current_diff = diff;
                ESP_LOGI(TAG, "Pool difficulty set to %g", diff);
                if (s_config.on_difficulty)
                    s_config.on_difficulty(diff, pool_id());
            }
        } else if (strcmp(method, "mining.set_version_mask") == 0) {
            uint32_t mask;
            if (stratum_parse_set_version_mask(line, &mask) == 0) {
                ESP_LOGI(TAG, "Version mask updated: 0x%08x", (unsigned)mask);
            }
        }
    } else {
        /* Response message - check for id and result */
        cJSON *root = cJSON_Parse(line);
        if (root) {
            cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
            if (cJSON_IsNumber(id_item)) {
                int id = id_item->valueint;
                if (remove_pending_id(id)) {
                    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
                    if (cJSON_IsTrue(result)) {
                        s_accepted++;
                        ESP_LOGI(TAG, "Share accepted (total: %lu)", (unsigned long)s_accepted);
                    } else {
                        s_rejected++;
                        cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
                        if (cJSON_IsArray(err) && cJSON_GetArraySize(err) >= 2) {
                            cJSON *msg = cJSON_GetArrayItem(err, 1);
                            ESP_LOGW(TAG, "Share rejected: %s (total: %lu)",
                                     cJSON_IsString(msg) ? msg->valuestring : "unknown",
                                     (unsigned long)s_rejected);
                        } else {
                            ESP_LOGW(TAG, "Share rejected (total: %lu)", (unsigned long)s_rejected);
                        }
                    }
                }
            }
            cJSON_Delete(root);
        }
    }
}

static bool do_connect(stratum_pool_config_t *pool)
{
    s_current_pool = pool;
    s_state = STRATUM_STATE_CONNECTING;
    ESP_LOGI(TAG, "Connecting to %s:%u (TLS=%d)", pool->pool_url, pool->pool_port, pool->use_tls);

    s_conn = stratum_connect(pool->pool_url, pool->pool_port, pool->use_tls);
    if (!s_conn) {
        ESP_LOGE(TAG, "Connection failed");
        s_state = STRATUM_STATE_ERROR;
        return false;
    }

    return true;
}

static bool do_subscribe(void)
{
    char buf[MSG_BUF_SIZE];
    char line[LINE_BUF_SIZE];

    s_state = STRATUM_STATE_SUBSCRIBING;

    int len = stratum_build_subscribe(buf, sizeof(buf));
    if (len < 0 || stratum_send(s_conn, buf, (size_t)len) < 0) {
        ESP_LOGE(TAG, "Subscribe send failed");
        return false;
    }

    if (stratum_recv_line(s_conn, line, sizeof(line), 30000) < 0) {
        ESP_LOGE(TAG, "Subscribe recv failed");
        return false;
    }

    if (stratum_parse_subscribe_result(line, &s_sub_result) != 0) {
        ESP_LOGE(TAG, "Failed to parse subscribe result");
        return false;
    }

    ESP_LOGI(TAG, "Subscribed: extranonce1=%s, en2_size=%d",
             s_sub_result.extranonce1, s_sub_result.extranonce2_size);
    return true;
}

static bool do_configure(void)
{
    char buf[MSG_BUF_SIZE];
    char line[LINE_BUF_SIZE];

    s_state = STRATUM_STATE_CONFIGURING;

    int len = stratum_build_configure(buf, sizeof(buf), VERSION_ROLLING_MASK);
    if (len < 0 || stratum_send(s_conn, buf, (size_t)len) < 0) {
        ESP_LOGE(TAG, "Configure send failed");
        return false;
    }

    if (stratum_recv_line(s_conn, line, sizeof(line), 30000) < 0) {
        ESP_LOGE(TAG, "Configure recv failed");
        return false;
    }

    uint32_t mask;
    if (stratum_parse_set_version_mask(line, &mask) == 0) {
        ESP_LOGI(TAG, "Version rolling mask: 0x%08x", (unsigned)mask);
    }

    return true;
}

static bool do_authorize(void)
{
    char buf[MSG_BUF_SIZE];
    char line[LINE_BUF_SIZE];

    s_state = STRATUM_STATE_AUTHORIZING;

    int len = stratum_build_authorize(buf, sizeof(buf),
                                      s_current_pool->pool_user,
                                      s_current_pool->pool_pass);
    if (len < 0 || stratum_send(s_conn, buf, (size_t)len) < 0) {
        ESP_LOGE(TAG, "Authorize send failed");
        return false;
    }

    if (stratum_recv_line(s_conn, line, sizeof(line), 30000) < 0) {
        ESP_LOGE(TAG, "Authorize recv failed");
        return false;
    }

    /* Check for result: true */
    cJSON *root = cJSON_Parse(line);
    if (root) {
        cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
        if (!cJSON_IsTrue(result)) {
            ESP_LOGE(TAG, "Authorization rejected");
            cJSON_Delete(root);
            return false;
        }
        cJSON_Delete(root);
    }

    ESP_LOGI(TAG, "Authorized as %s", s_current_pool->pool_user);
    return true;
}

esp_err_t stratum_client_init(const stratum_client_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;
    memcpy(&s_config, config, sizeof(s_config));
    s_state = STRATUM_STATE_DISCONNECTED;
    s_accepted = 0;
    s_rejected = 0;
    s_pending_count = 0;
    s_current_pool = &s_config.primary;
    return ESP_OK;
}

void stratum_client_task(void *param)
{
    (void)param;
    char line[LINE_BUF_SIZE];
    int retry_delay_s = 5;

    while (1) {
        /* Try primary pool */
        if (!do_connect(&s_config.primary)) {
            /* Try fallback if configured */
            if (s_config.fallback.pool_url[0]) {
                ESP_LOGW(TAG, "Primary failed, trying fallback");
                if (!do_connect(&s_config.fallback)) {
                    ESP_LOGE(TAG, "Fallback also failed, retrying in %ds", retry_delay_s);
                    vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
                    continue;
                }
            } else {
                ESP_LOGE(TAG, "Connection failed, retrying in %ds", retry_delay_s);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
                continue;
            }
        }

        /* Connection sequence: subscribe -> configure -> authorize */
        if (!do_subscribe() || !do_configure() || !do_authorize()) {
            stratum_disconnect(s_conn);
            s_conn = NULL;
            s_state = STRATUM_STATE_ERROR;
            vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
            continue;
        }

        /* Enter mining loop */
        s_state = STRATUM_STATE_MINING;
        ESP_LOGI(TAG, "Mining state reached, processing pool messages");

        while (stratum_is_connected(s_conn)) {
            int ret = stratum_recv_line(s_conn, line, sizeof(line), 60000);
            if (ret < 0) {
                ESP_LOGW(TAG, "Recv error or timeout, disconnecting");
                break;
            }
            if (ret > 0) {
                handle_line(line);
            }
        }

        /* Disconnected - clean up and retry */
        ESP_LOGW(TAG, "Disconnected from pool");
        stratum_disconnect(s_conn);
        s_conn = NULL;
        s_state = STRATUM_STATE_DISCONNECTED;
        vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
    }
}

stratum_state_t stratum_client_get_state(void)
{
    return s_state;
}

const char *stratum_client_get_extranonce1(void)
{
    return s_sub_result.extranonce1;
}

int stratum_client_get_extranonce2_size(void)
{
    return s_sub_result.extranonce2_size;
}

uint32_t stratum_client_get_accepted(void)
{
    return s_accepted;
}

uint32_t stratum_client_get_rejected(void)
{
    return s_rejected;
}

double stratum_client_get_current_difficulty(void)
{
    return s_current_diff;
}

esp_err_t stratum_client_submit_share(const char *job_id, const char *extranonce2,
                                      const char *ntime, const char *nonce,
                                      const char *version_bits)
{
    if (!s_conn || s_state != STRATUM_STATE_MINING)
        return ESP_ERR_INVALID_STATE;

    char buf[MSG_BUF_SIZE];

    /* ERRATA FIX: use current_pool->pool_user, not always primary */
    int len = stratum_build_submit(buf, sizeof(buf),
                                   s_current_pool->pool_user,
                                   job_id, extranonce2, ntime, nonce, version_bits);
    if (len < 0)
        return ESP_ERR_NO_MEM;

    /* Extract the id from the message we just built so we can track accept/reject.
     * The id is the value after "id": at the start of the JSON. */
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsNumber(id_item))
            add_pending_id(id_item->valueint);
        cJSON_Delete(root);
    }

    if (stratum_send(s_conn, buf, (size_t)len) < 0)
        return ESP_FAIL;

    return ESP_OK;
}
