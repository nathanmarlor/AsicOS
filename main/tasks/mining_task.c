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
#include <string.h>

static const char *TAG = "mining_task";

static QueueHandle_t      s_notify_queue;
static asic_job_t         s_active_jobs[128];
static SemaphoreHandle_t  s_jobs_mutex;
static double             s_current_pool_diff   = 1.0;
static uint32_t           s_extranonce2_counter  = 0;
static uint8_t            s_asic_job_id          = 0;  /* rotating ASIC job ID */

/* ---- Callbacks (called from stratum task context) ---- */

void mining_on_notify(const stratum_notify_t *notify, int pool_id)
{
    if (!s_notify_queue) {
        return;
    }
    /* Overwrite – we only care about the latest notify */
    xQueueOverwrite(s_notify_queue, notify);
}

void mining_on_difficulty(double diff, int pool_id)
{
    s_current_pool_diff = diff;
    ESP_LOGI(TAG, "Pool difficulty updated: %.4f (pool %d)", diff, pool_id);
}

/* ---- Job lookup (used by result_task) ---- */

const asic_job_t *mining_get_job(uint8_t job_id)
{
    if (job_id >= 128) {
        return NULL;
    }
    const asic_job_t *job = NULL;
    if (xSemaphoreTake(s_jobs_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_active_jobs[job_id].stratum_job_id[0] != '\0') {
            job = &s_active_jobs[job_id];
        }
        xSemaphoreGive(s_jobs_mutex);
    }
    return job;
}

/* ---- Main task ---- */

static void mining_task_fn(void *param)
{
    const board_config_t *board = board_get_config();
    const TickType_t job_interval = pdMS_TO_TICKS(board->job_interval_ms);

    ESP_LOGI(TAG, "Mining task started (job interval %" PRIu16 " ms)", board->job_interval_ms);

    stratum_notify_t notify;
    bool have_notify = false;

    for (;;) {
        /* Wait for a notify or timeout at the job interval */
        if (xQueueReceive(s_notify_queue, &notify, job_interval) == pdTRUE) {
            have_notify = true;
            ESP_LOGI(TAG, "New stratum notify: job_id=%s clean=%d",
                     notify.job_id, notify.clean_jobs);
        }

        /* Must be connected and have received at least one notify */
        if (stratum_client_get_state() != STRATUM_STATE_MINING || !have_notify) {
            continue;
        }

        /* Build ASIC job from latest notify */
        const char *extranonce1     = stratum_client_get_extranonce1();
        int         extranonce2_sz  = stratum_client_get_extranonce2_size();

        asic_job_t job;
        memset(&job, 0, sizeof(job));

        int rc = stratum_build_asic_job(&notify, extranonce1,
                                        s_extranonce2_counter, extranonce2_sz,
                                        s_current_pool_diff, 0, &job);
        if (rc != 0) {
            ESP_LOGE(TAG, "stratum_build_asic_job failed (%d)", rc);
            continue;
        }

        /* Assign rotating job ID (matches forge-os: id incremented BEFORE assign) */
        s_asic_job_id = (s_asic_job_id + 24) % 128;
        job.job_id = s_asic_job_id;

        /* Store job at s_active_jobs[job.job_id] (original ID, mutex protected) */
        if (xSemaphoreTake(s_jobs_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_active_jobs[job.job_id] = job;
            xSemaphoreGive(s_jobs_mutex);
        }

        /* Send work to ASIC */
        esp_err_t err = bm1370_send_work(&job);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bm1370_send_work failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Job sent: id=%u extranonce2=%" PRIu32 " diff=%.2f",
                     job.job_id, s_extranonce2_counter, s_current_pool_diff);
        }

        s_extranonce2_counter++;
    }
}

void mining_task_start(void)
{
    /* Queue depth 1 so xQueueOverwrite works (overwrites single slot) */
    s_notify_queue = xQueueCreate(1, sizeof(stratum_notify_t));
    s_jobs_mutex   = xSemaphoreCreateMutex();

    memset(s_active_jobs, 0, sizeof(s_active_jobs));

    xTaskCreate(mining_task_fn, "mining", MINING_TASK_STACK_SIZE,
                NULL, MINING_TASK_PRIORITY, NULL);
}
