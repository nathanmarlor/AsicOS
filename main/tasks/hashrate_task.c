#include "hashrate_task.h"
#include "asic.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "hashrate_task";

#define WARMUP_DELAY_MS   5000
#define POLL_INTERVAL_MS  10000
#define MEDIAN_TAPS       3

static hashrate_info_t s_info;

const hashrate_info_t *hashrate_task_get_info(void)
{
    return &s_info;
}

static float median_of_three(float a, float b, float c)
{
    if ((a >= b && a <= c) || (a <= b && a >= c)) return a;
    if ((b >= a && b <= c) || (b <= a && b >= c)) return b;
    return c;
}

static void hashrate_task_fn(void *param)
{
    const board_config_t *board = board_get_config();
    const asic_state_t *asic    = asic_get_state();

    ESP_LOGI(TAG, "Hashrate task started, warmup %d ms", WARMUP_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(WARMUP_DELAY_MS));

    int chip_count = asic->chip_count;
    if (chip_count <= 0) {
        chip_count = board->expected_chip_count;
    }
    if (chip_count > 16) {
        chip_count = 16;
    }

    s_info.chip_count = chip_count;

    /* Per-chip state */
    uint32_t prev_counter[16] = {0};
    float    history[16][MEDIAN_TAPS];
    float    smoothed[16];
    int      sample_idx = 0;
    bool     first_sample = true;

    memset(history, 0, sizeof(history));
    memset(smoothed, 0, sizeof(smoothed));

    /* Read initial counters */
    for (int i = 0; i < chip_count; i++) {
        prev_counter[i] = asic_read_hash_counter((uint8_t)i);
    }

    int64_t prev_time_us = (int64_t)(xTaskGetTickCount()) * (1000000LL / configTICK_RATE_HZ);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        int64_t now_us = (int64_t)(xTaskGetTickCount()) * (1000000LL / configTICK_RATE_HZ);
        int64_t dt_us  = now_us - prev_time_us;
        if (dt_us <= 0) {
            dt_us = (int64_t)POLL_INTERVAL_MS * 1000;
        }
        prev_time_us = now_us;

        float total_ghs = 0.0f;

        for (int i = 0; i < chip_count; i++) {
            uint32_t counter = asic_read_hash_counter((uint8_t)i);
            uint32_t delta   = counter - prev_counter[i];
            prev_counter[i]  = counter;

            /* The BM1370 hash counter (register 0x90) counts in units
             * where each tick represents ~1 MH (1e6 hashes).
             *
             * hashes/sec = delta * 1e6 / dt_seconds
             *            = delta * 1e6 / (dt_us / 1e6)
             *            = delta * 1e12 / dt_us
             *
             * GH/s = hashes/sec / 1e9 = delta * 1e3 / dt_us
             *
             * The previous formula erroneously applied a 2^32 multiplier
             * (assuming the counter counted nonce-space sweeps), which
             * inflated the reported hashrate by orders of magnitude.     */
            float raw_ghs = (float)((double)delta * 1e3 / (double)dt_us);

            /* Store in history ring for median filter */
            int tap = sample_idx % MEDIAN_TAPS;
            history[i][tap] = raw_ghs;

            float filtered;
            if (first_sample || sample_idx < MEDIAN_TAPS) {
                /* Not enough samples for median yet, use raw */
                filtered = raw_ghs;
            } else {
                filtered = median_of_three(history[i][0], history[i][1], history[i][2]);
            }

            /* 50/50 exponential smoothing */
            if (first_sample) {
                smoothed[i] = filtered;
            } else {
                smoothed[i] = 0.5f * smoothed[i] + 0.5f * filtered;
            }

            s_info.per_chip_hashrate_ghs[i] = smoothed[i];
            total_ghs += smoothed[i];
        }

        s_info.total_hashrate_ghs = total_ghs;
        sample_idx++;
        first_sample = false;

        ESP_LOGI(TAG, "Total hashrate: %.2f GH/s (%d chips)", total_ghs, chip_count);
    }
}

void hashrate_task_start(void)
{
    memset(&s_info, 0, sizeof(s_info));

    xTaskCreate(hashrate_task_fn, "hashrate", HASHRATE_TASK_STACK_SIZE,
                NULL, HASHRATE_TASK_PRIORITY, NULL);
}
