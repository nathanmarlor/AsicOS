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

static QueueHandle_t      s_notify_queue;
static asic_job_t         s_active_jobs[128];
static SemaphoreHandle_t  s_jobs_mutex;
static volatile double    s_current_pool_diff   = 1.0;
static uint32_t           s_extranonce2_counter  = 0;
static uint8_t            s_asic_job_id          = 0;  /* rotating ASIC job ID */
static volatile uint32_t  s_block_height         = 0;

/* Parse block height from coinbase1 hex (BIP34).
 * Coinbase structure: version(4) + txin_count(1) + prev_out(36) + script_len(1+) + script...
 * The script starts with a push of the block height in little-endian.
 * Byte 42*2 = offset 84 in hex = script_len, byte 43*2 = 86 = height_len, then height bytes. */
static uint32_t parse_block_height(const char *coinbase1_hex)
{
    size_t len = strlen(coinbase1_hex);
    /* We need at least to offset 84 (script_len) + a few more */
    if (len < 92) return 0;

    /* Script starts at byte offset 41 in the transaction (hex offset 82).
     * First byte of script is the number of bytes encoding the height. */
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
    if (!s_notify_queue) {
        return;
    }
    /* Parse block height from coinbase1 */
    uint32_t h = parse_block_height(notify->coinbase_1);
    if (h > 0) s_block_height = h;

    /* Overwrite – we only care about the latest notify */
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
    if (job_id >= 128 || !out) {
        return false;
    }
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

        /* Assign rotating job ID */
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
        } else if (s_extranonce2_counter % 100 == 0) {
            ESP_LOGI(TAG, "Jobs sent: %" PRIu32 " (latest id=%u diff=%.2f)",
                     s_extranonce2_counter, job.job_id, s_current_pool_diff);
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
