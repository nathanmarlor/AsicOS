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

static mining_stats_t s_stats;
static uint64_t s_nonce_count = 0;
static uint64_t s_per_chip_nonces[16] = {0};
static int64_t s_last_summary_time = 0;
static uint32_t s_nonces_since_summary = 0;

const mining_stats_t *result_task_get_stats(void)
{
    return &s_stats;
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

    /* Load persisted best difficulty */
    uint64_t stored_diff = nvs_config_get_u64(NVS_KEY_BEST_DIFF, 0);
    double best_diff_bits;
    memcpy(&best_diff_bits, &stored_diff, sizeof(double));
    if (!isfinite(best_diff_bits) || best_diff_bits < 0.0) {
        best_diff_bits = 0.0;
    }
    s_stats.best_difficulty = best_diff_bits;

    asic_result_t result;

    for (;;) {
        int rc = asic_receive_result(&result, RESULT_TIMEOUT_MS);
        if (rc <= 0) {
            /* Log periodically to aid debugging without spamming */
            static int no_response_count = 0;
            no_response_count++;
            if (no_response_count % 20 == 0) {
                ESP_LOGW(TAG, "No ASIC nonce in %d reads (%.0fs)",
                         no_response_count, no_response_count * 60.0 / 20);
            }
            continue;
        }

        /* Map ASIC job ID back to original job ID */
        uint8_t original_id = bm1370_asic_to_job_id(result.job_id);

        /* Lookup job */
        const asic_job_t *job = mining_get_job(original_id);
        if (!job) {
            ESP_LOGW(TAG, "No job found for ASIC id %u (original %u)", result.job_id, original_id);
            continue;
        }

        /* Check duplicate */
        if (is_duplicate(result.nonce, result.rolled_version)) {
            s_stats.duplicate_nonces++;
            ESP_LOGD(TAG, "Duplicate nonce 0x%08lx", (unsigned long)result.nonce);
            continue;
        }

        /* Count valid (non-duplicate) nonces */
        s_nonce_count++;
        int chip_nr = bm1370_nonce_to_chip(result.nonce, 2);
        if (chip_nr >= 0 && chip_nr < 16) s_per_chip_nonces[chip_nr]++;

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

        /* Periodic nonce summary instead of per-nonce logging */
        s_nonces_since_summary++;
        int64_t now = esp_timer_get_time();
        if (now - s_last_summary_time >= 10000000LL) { /* 10 seconds */
            ESP_LOGI(TAG, "Nonces: %lu in last 10s (latest diff=%.4f pool_diff=%.4f)",
                     (unsigned long)s_nonces_since_summary, share_diff, job->pool_diff);
            s_nonces_since_summary = 0;
            s_last_summary_time = now;
        }

        if (share_diff <= 0.0) {
            ESP_LOGW(TAG, "Invalid nonce test result (diff=%.6f)", share_diff);
            continue;
        }

        /* Update best difficulty */
        if (share_diff > s_stats.best_difficulty) {
            s_stats.best_difficulty = share_diff;
            ESP_LOGI(TAG, "New best difficulty: %.4f", share_diff);

            /* Persist to NVS */
            uint64_t diff_as_u64;
            memcpy(&diff_as_u64, &share_diff, sizeof(uint64_t));
            nvs_config_set_u64(NVS_KEY_BEST_DIFF, diff_as_u64);
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
