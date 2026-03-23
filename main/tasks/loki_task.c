#include "loki_task.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_http_client.h"

#include "nvs_config.h"

static const char *TAG = "loki_task";

#define RING_BUF_SIZE     64
#define MAX_MSG_LEN       128
#define MAX_LEVEL_LEN     8
#define LOKI_URL_MAX_LEN  256
#define PUSH_INTERVAL_MS  (10 * 1000)
#define IDLE_CHECK_MS     (30 * 1000)
#define JSON_BUF_SIZE     8192

typedef struct {
    char     level[MAX_LEVEL_LEN];
    char     msg[MAX_MSG_LEN];
    int64_t  timestamp_us;
} log_entry_t;

static log_entry_t s_ring[RING_BUF_SIZE];
static int s_head = 0;          /* next write position */
static int s_count = 0;         /* number of valid entries */
static SemaphoreHandle_t s_mutex = NULL;

/* ── Public: push a log entry (non-blocking) ───────────────────────── */

void loki_push_log(const char *level, const char *msg)
{
    if (!s_mutex) return;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return; /* drop if we can't get the lock quickly */
    }

    log_entry_t *entry = &s_ring[s_head];
    strncpy(entry->level, level, MAX_LEVEL_LEN - 1);
    entry->level[MAX_LEVEL_LEN - 1] = '\0';
    strncpy(entry->msg, msg, MAX_MSG_LEN - 1);
    entry->msg[MAX_MSG_LEN - 1] = '\0';
    entry->timestamp_us = esp_timer_get_time();

    s_head = (s_head + 1) % RING_BUF_SIZE;
    if (s_count < RING_BUF_SIZE) {
        s_count++;
    }
    /* If full, oldest is silently overwritten (ring buffer) */

    xSemaphoreGive(s_mutex);
}

/* ── Escape JSON string (minimal: backslash, quote, newline) ──────── */

static int json_escape(char *dst, size_t dst_len, const char *src)
{
    int w = 0;
    for (const char *p = src; *p && w < (int)dst_len - 1; p++) {
        switch (*p) {
        case '\\': if (w + 2 < (int)dst_len) { dst[w++] = '\\'; dst[w++] = '\\'; } break;
        case '"':  if (w + 2 < (int)dst_len) { dst[w++] = '\\'; dst[w++] = '"';  } break;
        case '\n': if (w + 2 < (int)dst_len) { dst[w++] = '\\'; dst[w++] = 'n';  } break;
        case '\r': if (w + 2 < (int)dst_len) { dst[w++] = '\\'; dst[w++] = 'r';  } break;
        default:   dst[w++] = *p; break;
        }
    }
    dst[w] = '\0';
    return w;
}

/* ── Build and send Loki push payload ─────────────────────────────── */

static esp_err_t loki_flush(const char *loki_url, const char *device_id)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_count == 0) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }

    /* Snapshot entries under lock */
    int count = s_count;
    int tail = (s_head - count + RING_BUF_SIZE) % RING_BUF_SIZE;

    log_entry_t *snapshot = malloc(count * sizeof(log_entry_t));
    if (!snapshot) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < count; i++) {
        snapshot[i] = s_ring[(tail + i) % RING_BUF_SIZE];
    }
    /* Don't clear yet — only clear after successful push */
    xSemaphoreGive(s_mutex);

    /* Build JSON */
    char *json = malloc(JSON_BUF_SIZE);
    if (!json) {
        free(snapshot);
        return ESP_ERR_NO_MEM;
    }

    char escaped_msg[MAX_MSG_LEN * 2];
    int off = 0;

    off += snprintf(json + off, JSON_BUF_SIZE - off,
        "{\"streams\":[{\"stream\":{\"job\":\"asicos\",\"host\":\"%s\"},\"values\":[",
        device_id);

    for (int i = 0; i < count; i++) {
        int64_t ts_ns = snapshot[i].timestamp_us * 1000LL; /* us -> ns */
        json_escape(escaped_msg, sizeof(escaped_msg), snapshot[i].msg);

        off += snprintf(json + off, JSON_BUF_SIZE - off,
            "%s[\"%lld\",\"level=%s %s\"]",
            (i > 0) ? "," : "",
            (long long)ts_ns,
            snapshot[i].level,
            escaped_msg);

        if (off >= JSON_BUF_SIZE - 64) {
            break; /* prevent overflow, send what we have */
        }
    }

    off += snprintf(json + off, JSON_BUF_SIZE - off, "]}]}");

    free(snapshot);

    /* Build full URL */
    char url[LOKI_URL_MAX_LEN + 32];
    snprintf(url, sizeof(url), "%s/loki/api/v1/push", loki_url);

    /* HTTP POST */
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(json);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json, off);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(json);

    if (err != ESP_OK || (status != 200 && status != 204)) {
        ESP_LOGW(TAG, "Loki push failed: err=%s status=%d", esp_err_to_name(err), status);
        return ESP_FAIL;
    }

    /* Success — clear the pushed entries from ring buffer */
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        /* Only remove the count we successfully pushed */
        if (s_count >= count) {
            s_count -= count;
        } else {
            s_count = 0;
        }
        xSemaphoreGive(s_mutex);
    }

    ESP_LOGD(TAG, "Pushed %d log entries to Loki", count);
    return ESP_OK;
}

/* ── Task loop ────────────────────────────────────────────────────── */

static void loki_task(void *param)
{
    char loki_url[LOKI_URL_MAX_LEN];
    char device_id[64];

    while (1) {
        nvs_config_get_string(NVS_KEY_LOKI_URL, loki_url, sizeof(loki_url), "");
        nvs_config_get_string(NVS_KEY_DEVICE_ID, device_id, sizeof(device_id), "asicos");

        if (strlen(loki_url) == 0) {
            /* Loki not configured — idle and check again later */
            vTaskDelay(pdMS_TO_TICKS(IDLE_CHECK_MS));
            continue;
        }

        /* Attempt flush */
        loki_flush(loki_url, device_id);

        vTaskDelay(pdMS_TO_TICKS(PUSH_INTERVAL_MS));
    }
}

/* ── Public: start the task ───────────────────────────────────────── */

void loki_task_start(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    xTaskCreate(loki_task, "loki_task", LOKI_TASK_STACK_SIZE, NULL, LOKI_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "Loki log push task started");
}
