#include "mining_task.h"
#include "stratum_client.h"
#include "stratum_job.h"
#include "bm1370.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mining_task";

/* ---- Shared state ---- */
static asic_job_t         s_active_jobs[128];
static SemaphoreHandle_t  s_jobs_mutex;
static volatile double    s_current_pool_diff   = 1.0;
static volatile uint32_t  s_block_height        = 0;

/* ---- Job queue (creator -> sender) ---- */
#define JOB_QUEUE_DEPTH   16
static QueueHandle_t      s_job_queue;         /* pre-built asic_job_t items */
static SemaphoreHandle_t  s_asic_semaphore;    /* wake sender early on new block */
static volatile bool      s_abandon_work       = false;

/* ---- Stratum notify queue (stratum callback -> creator) ---- */
static QueueHandle_t      s_notify_queue;

/* ---- Block height parsing ---- */
static uint32_t parse_block_height(const char *coinbase1_hex)
{
    size_t len = strlen(coinbase1_hex);
    if (len < 92) return 0;
    unsigned int height_byte_count = 0;
    char tmp[3] = { coinbase1_hex[84], coinbase1_hex[85], '\0' };
    if (sscanf(tmp, "%02x", &height_byte_count) != 1) return 0;
    if (height_byte_count == 0 || height_byte_count > 4) return 0;
    if (len < 86 + height_byte_count * 2) return 0;
    uint32_t height = 0;
    for (unsigned int i = 0; i < height_byte_count; i++) {
        unsigned int byte_val;
        char b[3] = { coinbase1_hex[86 + i * 2], coinbase1_hex[87 + i * 2], '\0' };
        if (sscanf(b, "%02x", &byte_val) != 1) return 0;
        height |= (byte_val << (i * 8));
    }
    return height;
}

/* ---- Callbacks (called from stratum task context) ---- */

void mining_on_notify(const stratum_notify_t *notify, int pool_id)
{
    if (!s_notify_queue) return;
    uint32_t h = parse_block_height(notify->coinbase_1);
    if (h > 0) s_block_height = h;

    if (notify->clean_jobs) {
        /* New block - abandon current work, flush job queue */
        s_abandon_work = true;
        xSemaphoreGive(s_asic_semaphore);  /* wake sender immediately */
    }

    xQueueOverwrite(s_notify_queue, notify);
}

uint32_t mining_get_block_height(void)
{
    return s_block_height;
}

void mining_on_difficulty(double diff, int pool_id)
{
    s_current_pool_diff = diff;
    ESP_LOGI(TAG, "Pool difficulty updated: %.4f (pool %d)", diff, pool_id);
}

/* ---- Job lookup (used by result_task) ---- */

bool mining_get_job(uint8_t job_id, asic_job_t *out)
{
    if (job_id >= 128 || !out) return false;
    if (xSemaphoreTake(s_jobs_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_active_jobs[job_id].stratum_job_id[0] != '\0') {
            memcpy(out, &s_active_jobs[job_id], sizeof(asic_job_t));
            xSemaphoreGive(s_jobs_mutex);
            return true;
        }
        xSemaphoreGive(s_jobs_mutex);
    }
    return false;
}

/* ---- Job creator task: pre-builds jobs into the queue ---- */

static void job_creator_fn(void *param)
{
    ESP_LOGI(TAG, "Job creator task started");

    stratum_notify_t notify;
    bool have_notify = false;
    uint32_t extranonce2_counter = 0;
    uint8_t asic_job_id = 0;

    for (;;) {
        /* Wait for a new notify or check periodically */
        if (xQueueReceive(s_notify_queue, &notify, pdMS_TO_TICKS(500)) == pdTRUE) {
            have_notify = true;
            ESP_LOGI(TAG, "New stratum notify: job_id=%s clean=%d",
                     notify.job_id, notify.clean_jobs);
        }

        if (stratum_client_get_state() != STRATUM_STATE_MINING || !have_notify) {
            continue;
        }

        /* If abandon was requested (new block), flush the queue */
        if (s_abandon_work) {
            s_abandon_work = false;
            /* Drain the job queue */
            asic_job_t discard;
            while (xQueueReceive(s_job_queue, &discard, 0) == pdTRUE) {}
            ESP_LOGI(TAG, "Work abandoned (new block), queue flushed");
        }

        /* Keep the queue topped up */
        while (uxQueueSpacesAvailable(s_job_queue) > 0 && !s_abandon_work) {
            const char *extranonce1 = stratum_client_get_extranonce1();
            int extranonce2_sz = stratum_client_get_extranonce2_size();

            asic_job_t job;
            memset(&job, 0, sizeof(job));

            int rc = stratum_build_asic_job(&notify, extranonce1,
                                            extranonce2_counter, extranonce2_sz,
                                            s_current_pool_diff, 0, &job);
            if (rc != 0) {
                ESP_LOGE(TAG, "stratum_build_asic_job failed (%d)", rc);
                break;
            }

            asic_job_id = (asic_job_id + 24) % 128;
            job.job_id = asic_job_id;

            /* Store in active jobs for result lookup */
            if (xSemaphoreTake(s_jobs_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_active_jobs[job.job_id] = job;
                xSemaphoreGive(s_jobs_mutex);
            }

            /* Enqueue for the ASIC sender */
            if (xQueueSend(s_job_queue, &job, pdMS_TO_TICKS(100)) != pdTRUE) {
                break;  /* Queue full, wait for sender to consume */
            }

            extranonce2_counter++;
        }
    }
}

/* ---- ASIC sender task: dequeues and sends jobs at the configured interval ---- */

static void asic_sender_fn(void *param)
{
    const board_config_t *board = board_get_config();
    const TickType_t job_interval = pdMS_TO_TICKS(board->job_interval_ms);
    uint32_t jobs_sent = 0;

    ESP_LOGI(TAG, "ASIC sender task started (interval %" PRIu16 " ms)", board->job_interval_ms);

    for (;;) {
        /* Wait for job interval OR early wake from new block semaphore */
        xSemaphoreTake(s_asic_semaphore, job_interval);

        /* If abandon requested, skip sending until fresh jobs arrive */
        if (s_abandon_work) {
            continue;
        }

        asic_job_t job;
        if (xQueueReceive(s_job_queue, &job, 0) != pdTRUE) {
            continue;  /* No jobs ready yet */
        }

        esp_err_t err = bm1370_send_work(&job);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bm1370_send_work failed: %s", esp_err_to_name(err));
        }

        jobs_sent++;
        if (jobs_sent % 100 == 0) {
            ESP_LOGI(TAG, "Jobs sent: %" PRIu32 " (latest id=%u diff=%.2f)",
                     jobs_sent, job.job_id, s_current_pool_diff);
        }
    }
}

/* ---- Start ---- */

void mining_task_start(void)
{
    s_notify_queue   = xQueueCreate(1, sizeof(stratum_notify_t));
    s_job_queue      = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(asic_job_t));
    s_jobs_mutex     = xSemaphoreCreateMutex();
    s_asic_semaphore = xSemaphoreCreateBinary();

    memset(s_active_jobs, 0, sizeof(s_active_jobs));

    xTaskCreate(job_creator_fn, "job_creator", MINING_TASK_STACK_SIZE,
                NULL, MINING_TASK_PRIORITY, NULL);
    xTaskCreate(asic_sender_fn, "asic_sender", 4096,
                NULL, MINING_TASK_PRIORITY + 1, NULL);  /* higher priority to send promptly */
}
