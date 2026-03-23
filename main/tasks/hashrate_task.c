#include "hashrate_task.h"
#include "asic.h"
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
#define RESPONSE_WAIT_MS 200

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

    ESP_LOGI(TAG, "Hashrate task started (HW counter method), warmup %d ms", WARMUP_MS);
    vTaskDelay(pdMS_TO_TICKS(WARMUP_MS));

    /* Read initial counter values (total + domain) */
    for (int i = 0; i < s_info.chip_count; i++) {
        asic_request_hash_counter((uint8_t)(i * 4));
        asic_request_domain_counters((uint8_t)(i * 4));
    }
    vTaskDelay(pdMS_TO_TICKS(RESPONSE_WAIT_MS));

    uint32_t prev_counter[16] = {0};
    uint32_t prev_domain_counter[16][HASHRATE_NUM_DOMAINS] = {{0}};
    for (int i = 0; i < s_info.chip_count; i++) {
        prev_counter[i] = asic_get_stored_hash_counter(i);
        for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
            prev_domain_counter[i][d] = asic_get_stored_domain_counter(i, d);
        }
    }
    int64_t prev_time_us = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        /* Send read requests for all chips (total + domain counters) */
        for (int i = 0; i < s_info.chip_count; i++) {
            asic_request_hash_counter((uint8_t)(i * 4));
            asic_request_domain_counters((uint8_t)(i * 4));
        }
        vTaskDelay(pdMS_TO_TICKS(RESPONSE_WAIT_MS));

        int64_t now_us = esp_timer_get_time();
        double dt_sec = (double)(now_us - prev_time_us) / 1e6;

        float total_ghs = 0.0f;

        for (int i = 0; i < s_info.chip_count; i++) {
            uint32_t counter = asic_get_stored_hash_counter(i);
            if (counter == 0 && prev_counter[i] == 0) {
                continue;  /* No data yet for this chip */
            }

            uint32_t delta = counter - prev_counter[i];  /* handles wrap */
            prev_counter[i] = counter;

            if (dt_sec > 0 && delta > 0) {
                /* GH/s = counter_delta / dt * 2^32 / 1e9 */
                float ghs = (float)delta / (float)dt_sec * 4294967296.0f / 1e9f;
                s_info.per_chip_hashrate_ghs[i] = ghs;
                total_ghs += ghs;
            }

            /* Per-domain hashrate */
            for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
                uint32_t d_counter = asic_get_stored_domain_counter(i, d);
                if (d_counter == 0 && prev_domain_counter[i][d] == 0) {
                    continue;
                }
                uint32_t d_delta = d_counter - prev_domain_counter[i][d];
                prev_domain_counter[i][d] = d_counter;
                if (dt_sec > 0 && d_delta > 0) {
                    s_info.per_domain_hashrate_ghs[i][d] =
                        (float)d_delta / (float)dt_sec * 4294967296.0f / 1e9f;
                }
            }
        }

        /* EMA smoothing */
        if (total_ghs > 0.0f) {
            if (s_info.total_hashrate_ghs == 0.0f) {
                s_info.total_hashrate_ghs = total_ghs;
            } else {
                s_info.total_hashrate_ghs =
                    (s_info.total_hashrate_ghs * (EMA_ALPHA - 1) + total_ghs) / EMA_ALPHA;
            }
        }

        prev_time_us = now_us;

        ESP_LOGI(TAG, "Hashrate: %.2f GH/s (%d chips, dt=%.1fs)",
                 s_info.total_hashrate_ghs, s_info.chip_count, dt_sec);
    }
}

void hashrate_task_start(void)
{
    memset(&s_info, 0, sizeof(s_info));

    xTaskCreate(hashrate_task_fn, "hashrate", HASHRATE_TASK_STACK_SIZE,
                NULL, HASHRATE_TASK_PRIORITY, NULL);
}
