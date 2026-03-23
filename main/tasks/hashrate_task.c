#include "hashrate_task.h"
#include "result_task.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "hashrate_task";

#define POLL_INTERVAL_MS 10000
#define WARMUP_MS        10000
#define EMA_ALPHA        12

/* ASIC internal difficulty set during init (BM1370 ticket mask = 256) */
#define ASIC_DIFFICULTY  256.0

static hashrate_info_t s_info;

const hashrate_info_t *hashrate_task_get_info(void)
{
    return &s_info;
}

static void hashrate_task_fn(void *param)
{
    const board_config_t *board = board_get_config();
    s_info.chip_count = board->expected_chip_count;
    if (s_info.chip_count > 16) {
        s_info.chip_count = 16;
    }

    ESP_LOGI(TAG, "Hashrate task started (nonce-rate method), warmup %d ms", WARMUP_MS);
    vTaskDelay(pdMS_TO_TICKS(WARMUP_MS));

    uint64_t prev_nonces = result_task_get_nonce_count();
    int64_t prev_time_us = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        uint64_t nonces = result_task_get_nonce_count();
        int64_t now_us = esp_timer_get_time();

        uint64_t delta_nonces = nonces - prev_nonces;
        double dt_sec = (double)(now_us - prev_time_us) / 1e6;

        if (dt_sec > 0 && delta_nonces > 0) {
            /* Each nonce represents ASIC_DIFFICULTY * 2^32 hashes on average */
            double total_hashes = (double)delta_nonces * ASIC_DIFFICULTY * 4294967296.0;
            double ghs = total_hashes / dt_sec / 1e9;

            /* EMA smoothing */
            if (s_info.total_hashrate_ghs == 0.0f) {
                s_info.total_hashrate_ghs = (float)ghs;
            } else {
                s_info.total_hashrate_ghs =
                    (s_info.total_hashrate_ghs * (EMA_ALPHA - 1) + (float)ghs) / EMA_ALPHA;
            }

            /* Split evenly across chips (no per-chip nonce tracking) */
            for (int i = 0; i < s_info.chip_count; i++) {
                s_info.per_chip_hashrate_ghs[i] =
                    s_info.total_hashrate_ghs / s_info.chip_count;
            }
        }

        prev_nonces = nonces;
        prev_time_us = now_us;

        ESP_LOGI(TAG, "Hashrate: %.2f GH/s (%" PRIu64 " nonces in %.1fs)",
                 s_info.total_hashrate_ghs, delta_nonces, dt_sec);
    }
}

void hashrate_task_start(void)
{
    memset(&s_info, 0, sizeof(s_info));

    xTaskCreate(hashrate_task_fn, "hashrate", HASHRATE_TASK_STACK_SIZE,
                NULL, HASHRATE_TASK_PRIORITY, NULL);
}
