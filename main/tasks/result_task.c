#include "result_task.h"
#include "mining_task.h"
#include "stratum_client.h"
#include "stratum_job.h"
#include "bm1370.h"
#include "mining.h"
#include "nvs_config.h"
#include "led_status.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <math.h>
#include <arpa/inet.h>

static const char *TAG = "result_task";

#define DEDUP_RING_SIZE 32
#define RESULT_TIMEOUT_MS 60000

typedef struct {
    uint32_t nonce;
    uint16_t version;
} dedup_entry_t;

static dedup_entry_t s_dedup_ring[DEDUP_RING_SIZE];
static int           s_dedup_index = 0;

static volatile mining_stats_t s_stats;
static volatile uint64_t s_nonce_count = 0;
static uint64_t s_per_chip_nonces[16] = {0};
static uint64_t s_per_chip_hw_errors[16] = {0};
static volatile uint64_t s_total_hw_errors = 0;
static int64_t s_last_summary_time = 0;
static uint32_t s_nonces_since_summary = 0;
static double s_last_valid_diff = 0.0;
static volatile double s_last_share_diff = 0.0;  /* last submitted share difficulty */

/* Ring buffer of recent nonce difficulties for the share feed */
#define RECENT_NONCES_SIZE 32
typedef struct {
    double   diff;
    bool     submitted;
    uint32_t uptime_sec;  /* seconds since boot */
} recent_nonce_t;
static recent_nonce_t s_recent_nonces[RECENT_NONCES_SIZE];
static int s_recent_idx = 0;
static volatile uint32_t s_recent_seq = 0;  /* sequence number for change detection */

/* Rolling per-chip nonce counts: two alternating 30-minute buckets.
 * Report current + previous for a ~30-60 min sliding window.
 * Every 30 min, previous = current, current = zeroed. */
#define BUCKET_WINDOW_US  (1800LL * 1000000LL)  /* 30 minutes */
static uint32_t s_nonce_bucket_cur[16] = {0};
static uint32_t s_nonce_bucket_prev[16] = {0};
static int64_t s_bucket_swap_time = 0;

/* Rolling share rate: ring buffer of share timestamps (last hour) */
#define SHARE_TS_RING_SIZE 256
static int64_t s_share_timestamps[SHARE_TS_RING_SIZE];
static int s_share_ts_idx = 0;
static int s_share_ts_count = 0;

const mining_stats_t *result_task_get_stats(void)
{
    return (const mining_stats_t *)&s_stats;
}

uint64_t result_task_get_nonce_count(void)
{
    return s_nonce_count;
}

uint64_t result_task_get_chip_nonce_count(int chip)
{
    if (chip >= 0 && chip < 16) return s_per_chip_nonces[chip];
    return 0;
}

uint32_t result_task_get_chip_rolling_nonces(int chip)
{
    if (chip >= 0 && chip < 16) return s_nonce_bucket_cur[chip] + s_nonce_bucket_prev[chip];
    return 0;
}

uint64_t result_task_get_chip_hw_errors(int chip)
{
    if (chip >= 0 && chip < 16) return s_per_chip_hw_errors[chip];
    return 0;
}

uint64_t result_task_get_total_hw_errors(void)
{
    return s_total_hw_errors;
}

double result_task_get_last_share_diff(void)
{
    return s_last_share_diff;
}

uint32_t result_task_get_recent_nonces_seq(void)
{
    return s_recent_seq;
}

/* Copy recent nonces into caller's arrays, newest first. Returns count. */
int result_task_get_recent_nonces(double *diffs, bool *submitted, uint32_t *uptime_secs, int max_count)
{
    int count = (s_recent_seq < RECENT_NONCES_SIZE) ? (int)s_recent_seq : RECENT_NONCES_SIZE;
    if (count > max_count) count = max_count;
    for (int i = 0; i < count; i++) {
        int idx = (s_recent_idx - 1 - i + RECENT_NONCES_SIZE) % RECENT_NONCES_SIZE;
        diffs[i] = s_recent_nonces[idx].diff;
        submitted[i] = s_recent_nonces[idx].submitted;
        uptime_secs[i] = s_recent_nonces[idx].uptime_sec;
    }
    return count;
}

float result_task_get_hw_error_rate(void)
{
    uint64_t total = s_nonce_count + s_total_hw_errors;
    if (total == 0) return 0.0f;
    return (float)s_total_hw_errors / (float)total * 100.0f;
}

static void record_share_timestamp(void)
{
    s_share_timestamps[s_share_ts_idx] = esp_timer_get_time();
    s_share_ts_idx = (s_share_ts_idx + 1) % SHARE_TS_RING_SIZE;
    if (s_share_ts_count < SHARE_TS_RING_SIZE) s_share_ts_count++;
}

float result_task_get_share_rate(void)
{
    if (s_share_ts_count < 2) return 0.0f;
    int64_t now = esp_timer_get_time();
    int64_t one_hour_ago = now - 3600000000LL;  /* 1 hour in microseconds */
    int count = 0;
    for (int i = 0; i < s_share_ts_count; i++) {
        if (s_share_timestamps[i] >= one_hour_ago) count++;
    }
    /* Shares in the last hour / minutes elapsed (capped at 60 min) */
    int64_t oldest_in_window = now;
    for (int i = 0; i < s_share_ts_count; i++) {
        if (s_share_timestamps[i] >= one_hour_ago && s_share_timestamps[i] < oldest_in_window)
            oldest_in_window = s_share_timestamps[i];
    }
    float minutes = (float)(now - oldest_in_window) / 60000000.0f;
    if (minutes < 0.5f) return 0.0f;
    return (float)count / minutes;
}

static bool is_duplicate(uint32_t nonce, uint16_t version)
{
    for (int i = 0; i < DEDUP_RING_SIZE; i++) {
        if (s_dedup_ring[i].nonce == nonce && s_dedup_ring[i].version == version) {
            return true;
        }
    }
    /* Add to ring */
    s_dedup_ring[s_dedup_index].nonce   = nonce;
    s_dedup_ring[s_dedup_index].version = version;
    s_dedup_index = (s_dedup_index + 1) % DEDUP_RING_SIZE;
    return false;
}

static void result_task_fn(void *param)
{
    ESP_LOGI(TAG, "Result task started");

    /* Load persisted all-time best difficulty from NVS */
    uint64_t stored_diff = nvs_config_get_u64(NVS_KEY_BEST_DIFF, 0);
    double alltime_best;
    memcpy(&alltime_best, &stored_diff, sizeof(double));
    if (!isfinite(alltime_best) || alltime_best < 0.0) {
        alltime_best = 0.0;
    }
    s_stats.alltime_best_diff = alltime_best;
    s_stats.session_best_diff = 0.0;

    asic_result_t result;

    for (;;) {
        int rc = asic_receive_result(&result, RESULT_TIMEOUT_MS);
        if (rc <= 0) {
            continue;
        }

        /* Map ASIC job ID back to original job ID */
        uint8_t original_id = bm1370_asic_to_job_id(result.job_id);

        /* Lookup job (copy into local buffer while mutex is held) */
        asic_job_t job_buf;
        if (!mining_get_job(original_id, &job_buf)) {
            ESP_LOGW(TAG, "No job found for ASIC id %u (original %u)", result.job_id, original_id);
            continue;
        }
        const asic_job_t *job = &job_buf;

        /* Check duplicate */
        if (is_duplicate(result.nonce, result.rolled_version)) {
            s_stats.duplicate_nonces++;
            ESP_LOGD(TAG, "Duplicate nonce 0x%08lx", (unsigned long)result.nonce);
            continue;
        }

        /* Count valid (non-duplicate) nonces */
        s_nonce_count++;

        /* Transition LED to MINING on first valid nonce */
        if (s_nonce_count == 1) {
            led_set_state(LED_STATE_MINING);
        }

        int chip_nr = bm1370_nonce_to_chip(result.nonce, 2);
        if (chip_nr >= 0 && chip_nr < 16) {
            s_per_chip_nonces[chip_nr]++;
            s_nonce_bucket_cur[chip_nr]++;
        }

        /* Swap buckets every 30 min: prev = cur, cur = zeroed.
         * Reported value is cur + prev = ~30-60 min sliding window. */
        int64_t now_roll = esp_timer_get_time();
        if (s_bucket_swap_time == 0) {
            s_bucket_swap_time = now_roll;
        } else if (now_roll - s_bucket_swap_time >= BUCKET_WINDOW_US) {
            memcpy(s_nonce_bucket_prev, s_nonce_bucket_cur, sizeof(s_nonce_bucket_prev));
            memset(s_nonce_bucket_cur, 0, sizeof(s_nonce_bucket_cur));
            s_bucket_swap_time = now_roll;
        }

        /* Build block header and test nonce with version rolling.
         * BM1370 nonce processing:
         *   version_bits = ntohs(asic_result.job.version) << 13
         *   rolled_version = job->version | version_bits
         */
        uint32_t version_bits = (uint32_t)ntohs(result.rolled_version) << 13;
        uint32_t rolled_version = job->version | version_bits;

        /* Build header for nonce verification:
         * memcpy fields directly from the job struct with no extra swaps.
         * The job's prev_block_hash and merkle_root are already in the
         * correct byte order for block header hashing. */
        uint8_t header[80];
        memcpy(header,      &rolled_version,      4);   /* version: LE uint32 */
        memcpy(header + 4,  job->prev_block_hash, 32);  /* prev_hash as-is */
        memcpy(header + 36, job->merkle_root,      32);  /* merkle_root as-is */
        memcpy(header + 68, &job->ntime,           4);   /* ntime: LE uint32 */
        memcpy(header + 72, &job->nbits,           4);   /* nbits: LE uint32 */
        memcpy(header + 76, &result.nonce,         4);   /* nonce: raw ASIC bytes */

        double share_diff = 0.0;
        mining_test_nonce(header, &share_diff);

        if (share_diff <= 0.0) {
            if (chip_nr >= 0 && chip_nr < 16) s_per_chip_hw_errors[chip_nr]++;
            s_total_hw_errors++;
        } else {
            s_last_valid_diff = share_diff;
        }

        /* Periodic nonce summary (after HW error counting for accuracy) */
        s_nonces_since_summary++;
        int64_t now = esp_timer_get_time();
        if (now - s_last_summary_time >= 10000000LL) { /* 10 seconds */
            ESP_LOGI(TAG, "Nonces: %lu in last 10s (diff=%.1f pool=%.0f err=%llu)",
                     (unsigned long)s_nonces_since_summary, s_last_valid_diff,
                     job->pool_diff, (unsigned long long)s_total_hw_errors);
            s_nonces_since_summary = 0;
            s_last_summary_time = now;
        }

        if (share_diff <= 0.0) {
            continue;
        }

        /* Update best difficulties */
        if (share_diff > s_stats.session_best_diff) {
            s_stats.session_best_diff = share_diff;
            ESP_LOGI(TAG, "New session best difficulty: %.4f", share_diff);
        }
        if (share_diff > s_stats.alltime_best_diff) {
            s_stats.alltime_best_diff = share_diff;
            ESP_LOGI(TAG, "New all-time best difficulty: %.4f", share_diff);

            /* Debounce NVS write to at most once per 60s */
            static int64_t s_last_best_diff_write = 0;
            int64_t now_us = esp_timer_get_time();
            if (now_us - s_last_best_diff_write >= 60000000LL) { /* 60 seconds */
                uint64_t diff_as_u64;
                memcpy(&diff_as_u64, &share_diff, sizeof(uint64_t));
                nvs_config_set_u64(NVS_KEY_BEST_DIFF, diff_as_u64);
                s_last_best_diff_write = now_us;
            }
        }

        /* If share meets pool difficulty, submit */
        if (share_diff >= job->pool_diff) {
            char nonce_hex[9];
            char version_hex[9];
            snprintf(nonce_hex, sizeof(nonce_hex), "%08lx", (unsigned long)result.nonce);
            /* Submit version rolling MASK (delta), not the full rolled version.
             * rolled_version ^ job->version */
            uint32_t version_mask = rolled_version ^ job->version;
            snprintf(version_hex, sizeof(version_hex), "%08lx", (unsigned long)version_mask);

            char ntime_hex[9];
            snprintf(ntime_hex, sizeof(ntime_hex), "%08lx", (unsigned long)job->ntime);

            esp_err_t err = stratum_client_submit_share(
                job->stratum_job_id, job->extranonce2,
                ntime_hex, nonce_hex, version_hex);

            if (err == ESP_OK) {
                s_stats.total_shares_submitted++;
                s_last_share_diff = share_diff;
                record_share_timestamp();
                /* Record in submitted shares ring buffer */
                s_recent_nonces[s_recent_idx].diff = share_diff;
                s_recent_nonces[s_recent_idx].submitted = true;
                s_recent_nonces[s_recent_idx].uptime_sec = (uint32_t)(esp_timer_get_time() / 1000000);
                s_recent_idx = (s_recent_idx + 1) % RECENT_NONCES_SIZE;
                s_recent_seq++;
                led_flash();  /* Brief LED flash on share submission */
                ESP_LOGI(TAG, "Share submitted: diff=%.4f pool_diff=%.4f nonce=0x%s",
                         share_diff, job->pool_diff, nonce_hex);
            } else {
                ESP_LOGW(TAG, "Share submission failed: %s", esp_err_to_name(err));
            }
        }
    }
}

void result_task_start(void)
{
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_dedup_ring, 0, sizeof(s_dedup_ring));

    xTaskCreate(result_task_fn, "result", RESULT_TASK_STACK_SIZE,
                NULL, RESULT_TASK_PRIORITY, NULL);
}
