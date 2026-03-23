#include "hashrate_task.h"
#include "asic.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "hashrate_task";

/* Matching forge-os hashrate_monitor_task.c exactly */
#define POLL_RATE 5000
#define EMA_ALPHA 12

/* forge-os: Hash counters are incremented on difficulty 1 (2^32 hashes) */
#define HASH_CNT_LSB 0x100000000uLL

static hashrate_info_t s_info;

const hashrate_info_t *hashrate_task_get_info(void)
{
    return &s_info;
}

/* Exact copy of forge-os hash_counter_to_ghs() */
static float hash_counter_to_ghs(uint32_t duration_ms, uint32_t counter)
{
    if (duration_ms == 0) return 0.0f;
    float seconds = duration_ms / 1000.0;
    float hashrate = counter / seconds * (float)HASH_CNT_LSB; // Make sure it stays in float
    return hashrate / 1e9f; // Convert to Gh/s
}

static void hashrate_task_fn(void *param)
{
    const board_config_t *board = board_get_config();
    const asic_state_t *asic    = asic_get_state();

    ESP_LOGI(TAG, "Hashrate task started, warmup %d ms", POLL_RATE);
    vTaskDelay(pdMS_TO_TICKS(POLL_RATE));

    int chip_count = asic->chip_count;
    if (chip_count <= 0) {
        chip_count = board->expected_chip_count;
    }
    if (chip_count > 16) {
        chip_count = 16;
    }

    s_info.chip_count = chip_count;

    /* Per-chip state matching forge-os update_hash_counter() */
    uint32_t prev_counter[16] = {0};
    uint32_t prev_time_ms[16] = {0};
    bool     first_sample = true;

    /* Read initial counters and record time */
    uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    for (int i = 0; i < chip_count; i++) {
        prev_counter[i] = asic_read_hash_counter((uint8_t)i);
        prev_time_ms[i] = now_ms;
    }

    float hashrate = 0.0f;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_RATE));

        now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        float total_ghs = 0.0f;

        for (int i = 0; i < chip_count; i++) {
            uint32_t counter = asic_read_hash_counter((uint8_t)i);

            if (prev_time_ms[i] != 0 && !first_sample) {
                /* Matching forge-os update_hash_counter() exactly */
                uint32_t duration_ms = now_ms - prev_time_ms[i];
                uint32_t counter_delta = counter - prev_counter[i]; // uint32_t wraparound handled

                float chip_hashrate = hash_counter_to_ghs(duration_ms, counter_delta);
                s_info.per_chip_hashrate_ghs[i] = chip_hashrate;
                total_ghs += chip_hashrate;

                ESP_LOGD(TAG, "ASIC %d: counter delta=%"PRIu32", duration=%"PRIu32"ms, hashrate=%.2f GH/s",
                         i, counter_delta, duration_ms, chip_hashrate);
            } else {
                ESP_LOGD(TAG, "ASIC %d: First measurement, storing value=0x%08"PRIX32", time=%"PRIu32,
                         i, counter, now_ms);
            }

            prev_counter[i] = counter;
            prev_time_ms[i] = now_ms;
        }

        if (!first_sample) {
            /* Matching forge-os EMA smoothing exactly */
            if (total_ghs == 0.0) {
                hashrate = 0.0;
            } else {
                if (hashrate == 0.0f) {
                    /* Initialize with current hashrate */
                    hashrate = total_ghs;
                    ESP_LOGI(TAG, "Initial hashrate set to %.2f GH/s", hashrate);
                } else {
                    hashrate = ((hashrate * (EMA_ALPHA - 1)) + total_ghs) / EMA_ALPHA;
                    ESP_LOGI(TAG, "EMA hashrate: %.2f GH/s", hashrate);
                }
            }
            s_info.total_hashrate_ghs = hashrate;
        }

        first_sample = false;

        ESP_LOGI(TAG, "Total hashrate: %.2f GH/s (%d chips)", hashrate, chip_count);
    }
}

void hashrate_task_start(void)
{
    memset(&s_info, 0, sizeof(s_info));

    xTaskCreate(hashrate_task_fn, "hashrate", HASHRATE_TASK_STACK_SIZE,
                NULL, HASHRATE_TASK_PRIORITY, NULL);
}
