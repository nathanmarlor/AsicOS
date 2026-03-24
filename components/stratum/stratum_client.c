#include "stratum_client.h"
#include "stratum_transport.h"
#include "stratum_api.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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
static SemaphoreHandle_t       s_conn_mutex;
static volatile bool           s_reconnect_requested = false;

/* Primary pool heartbeat when using fallback */
static int64_t s_last_primary_check_us = 0;
#define PRIMARY_CHECK_INTERVAL_US (60LL * 1000000LL)

/* Track pending submit message IDs for accept/reject counting + RTT */
#define MAX_PENDING_IDS 64
typedef struct {
    int     id;
    int64_t submit_time_us;
} pending_share_t;
static pending_share_t s_pending[MAX_PENDING_IDS];
static int s_pending_count;

/* RTT tracking (EMA) */
static float s_rtt_ema_ms = 0.0f;
#define RTT_EMA_ALPHA 0.2f

/* Rejection reasons */
static stratum_rejection_reasons_t s_reject_reasons;

/* Block tracking */
static uint32_t s_block_count = 0;

static void add_pending_id(int id)
{
    if (s_pending_count < MAX_PENDING_IDS) {
        s_pending[s_pending_count].id = id;
        s_pending[s_pending_count].submit_time_us = esp_timer_get_time();
        s_pending_count++;
    }
}

static int64_t remove_pending_id(int id)
{
    for (int i = 0; i < s_pending_count; i++) {
        if (s_pending[i].id == id) {
            int64_t t = s_pending[i].submit_time_us;
            s_pending[i] = s_pending[--s_pending_count];
            return t;
        }
    }
    return -1;
}

static void classify_rejection(const char *msg)
{
    if (!msg) { s_reject_reasons.other++; return; }
    if (strcasestr(msg, "job not found") || strcasestr(msg, "stale"))
        s_reject_reasons.job_not_found++;
    else if (strcasestr(msg, "duplicate"))
        s_reject_reasons.duplicate++;
    else if (strcasestr(msg, "low difficulty") || strcasestr(msg, "difficulty"))
        s_reject_reasons.low_difficulty++;
    else
        s_reject_reasons.other++;
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
            if (stratum_parse_notify(line, &notify) == 0) {
                if (notify.clean_jobs) s_block_count++;
                if (s_config.on_notify) s_config.on_notify(&notify, pool_id());
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
        } else if (strcmp(method, "client.reconnect") == 0) {
            ESP_LOGI(TAG, "Pool requested reconnect");
            s_reconnect_requested = true;
        }
    } else {
        /* Response message - check for id and result */
        cJSON *root = cJSON_Parse(line);
        if (root) {
            cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
            if (cJSON_IsNumber(id_item)) {
                int id = id_item->valueint;
                int64_t submit_time = remove_pending_id(id);
                if (submit_time >= 0) {
                    /* Calculate RTT */
                    float rtt_ms = (float)(esp_timer_get_time() - submit_time) / 1000.0f;
                    if (s_rtt_ema_ms == 0.0f)
                        s_rtt_ema_ms = rtt_ms;
                    else
                        s_rtt_ema_ms = s_rtt_ema_ms * (1.0f - RTT_EMA_ALPHA) + rtt_ms * RTT_EMA_ALPHA;

                    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
                    if (cJSON_IsTrue(result)) {
                        s_accepted++;
                        ESP_LOGI(TAG, "Share accepted (total: %lu, rtt: %.0fms)",
                                 (unsigned long)s_accepted, rtt_ms);
                    } else {
                        s_rejected++;
                        const char *reason = "unknown";
                        cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
                        if (cJSON_IsArray(err) && cJSON_GetArraySize(err) >= 2) {
                            cJSON *msg = cJSON_GetArrayItem(err, 1);
                            reason = cJSON_IsString(msg) ? msg->valuestring : "unknown";
                        }
                        classify_rejection(reason);
                        ESP_LOGW(TAG, "Share rejected: %s (total: %lu)",
                                 reason, (unsigned long)s_rejected);
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
    if (!s_conn_mutex) s_conn_mutex = xSemaphoreCreateMutex();
    s_pending_count = 0;
    s_current_pool = &s_config.primary;
    return ESP_OK;
}

void stratum_client_task(void *param)
{
    (void)param;
    char line[LINE_BUF_SIZE];

    /* Exponential backoff: start 5s, double each retry, cap at 300s, reset on success */
    #define BACKOFF_INITIAL_S   5
    #define BACKOFF_MAX_S     300
    int retry_delay_s = BACKOFF_INITIAL_S;

    while (1) {
        /* Try primary pool */
        if (!do_connect(&s_config.primary)) {
            /* Try fallback if configured */
            if (s_config.fallback.pool_url[0]) {
                ESP_LOGW(TAG, "Primary failed, trying fallback");
                if (!do_connect(&s_config.fallback)) {
                    ESP_LOGE(TAG, "Fallback also failed, retrying in %ds", retry_delay_s);
                    vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
                    retry_delay_s = (retry_delay_s * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : retry_delay_s * 2;
                    continue;
                }
            } else {
                ESP_LOGE(TAG, "Connection failed, retrying in %ds", retry_delay_s);
                vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
                retry_delay_s = (retry_delay_s * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : retry_delay_s * 2;
                continue;
            }
        }

        /* Connection sequence: subscribe -> configure -> authorize */
        if (!do_subscribe() || !do_configure() || !do_authorize()) {
            stratum_disconnect(s_conn);
            s_conn = NULL;
            s_state = STRATUM_STATE_ERROR;
            ESP_LOGE(TAG, "Handshake failed, retrying in %ds", retry_delay_s);
            vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
            retry_delay_s = (retry_delay_s * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : retry_delay_s * 2;
            continue;
        }

        /* Successfully connected - reset backoff */
        retry_delay_s = BACKOFF_INITIAL_S;

        /* Suggest difficulty to pool based on ASIC difficulty */
        {
            char buf[MSG_BUF_SIZE];
            int len = stratum_build_suggest_difficulty(buf, sizeof(buf), 256.0);
            if (len > 0 && s_conn) {
                stratum_send(s_conn, buf, (size_t)len);
                ESP_LOGI(TAG, "Suggested difficulty 256 to pool");
            }
        }

        /* Enter mining loop */
        s_state = STRATUM_STATE_MINING;
        ESP_LOGI(TAG, "Mining state reached, processing pool messages");

        while (stratum_is_connected(s_conn)) {
            if (s_reconnect_requested) {
                s_reconnect_requested = false;
                ESP_LOGI(TAG, "Reconnecting as requested by pool");
                break;
            }

            /* Periodically check if primary pool is back when using fallback */
            if (s_current_pool == &s_config.fallback && s_config.primary.pool_url[0]) {
                int64_t now_us = esp_timer_get_time();
                if (now_us - s_last_primary_check_us >= PRIMARY_CHECK_INTERVAL_US) {
                    s_last_primary_check_us = now_us;
                    /* Try a quick TCP probe to primary */
                    stratum_conn_t *probe = stratum_connect(
                        s_config.primary.pool_url,
                        s_config.primary.pool_port,
                        s_config.primary.use_tls);
                    if (probe) {
                        ESP_LOGI(TAG, "Primary pool is back, switching...");
                        stratum_disconnect(probe);
                        break;  /* exit mining loop to reconnect to primary */
                    }
                }
            }

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
        ESP_LOGW(TAG, "Disconnected from pool, retrying in %ds", retry_delay_s);
        xSemaphoreTake(s_conn_mutex, portMAX_DELAY);
        stratum_disconnect(s_conn);
        s_conn = NULL;
        s_state = STRATUM_STATE_DISCONNECTED;
        xSemaphoreGive(s_conn_mutex);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_s * 1000));
        retry_delay_s = (retry_delay_s * 2 > BACKOFF_MAX_S) ? BACKOFF_MAX_S : retry_delay_s * 2;
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
    if (xSemaphoreTake(s_conn_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    if (!s_conn || s_state != STRATUM_STATE_MINING) {
        xSemaphoreGive(s_conn_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    char buf[MSG_BUF_SIZE];

    int len = stratum_build_submit(buf, sizeof(buf),
                                   s_current_pool->pool_user,
                                   job_id, extranonce2, ntime, nonce, version_bits);
    if (len < 0) {
        xSemaphoreGive(s_conn_mutex);
        return ESP_ERR_NO_MEM;
    }

    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsNumber(id_item))
            add_pending_id(id_item->valueint);
        cJSON_Delete(root);
    }

    esp_err_t ret = (stratum_send(s_conn, buf, (size_t)len) < 0) ? ESP_FAIL : ESP_OK;
    xSemaphoreGive(s_conn_mutex);
    return ret;
}

float stratum_client_get_rtt_ms(void)
{
    return s_rtt_ema_ms;
}

const stratum_rejection_reasons_t *stratum_client_get_rejection_reasons(void)
{
    return &s_reject_reasons;
}

uint32_t stratum_client_get_block_count(void)
{
    return s_block_count;
}
