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

#define POLL_INTERVAL_MS 5000
#define WARMUP_MS        10000
#define EMA_ALPHA        12

static volatile hashrate_info_t s_info;
static volatile bool s_reset_requested = false;

void hashrate_task_reset(void) {
    s_reset_requested = true;
}

const hashrate_info_t *hashrate_task_get_info(void)
{
    return (const hashrate_info_t *)&s_info;
}

static void hashrate_task_fn(void *param)
{
    const board_config_t *board = board_get_config();
    s_info.chip_count = board->expected_chip_count;
    if (s_info.chip_count > 16) {
        s_info.chip_count = 16;
    }

    ESP_LOGI(TAG, "Hashrate task started (callback method), warmup %d ms", WARMUP_MS);
    vTaskDelay(pdMS_TO_TICKS(WARMUP_MS));

    /* Send initial register read requests to seed the baseline */
    for (int i = 0; i < s_info.chip_count; i++) {
        asic_request_hash_counter((uint8_t)(i * 4));
        asic_request_domain_counters((uint8_t)(i * 4));
    }

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));

        /* Handle reset request */
        if (s_reset_requested) {
            s_reset_requested = false;
            ESP_LOGI(TAG, "Reset requested – clearing measurements");
            s_info.total_hashrate_ghs = 0.0f;
            for (int i = 0; i < s_info.chip_count; i++) {
                s_info.per_chip_hashrate_ghs[i] = 0.0f;
                for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
                    s_info.per_domain_hashrate_ghs[i][d] = 0.0f;
                }
            }
            asic_reset_hashrate_measurements();
            /* Re-seed baselines */
            for (int i = 0; i < s_info.chip_count; i++) {
                asic_request_hash_counter((uint8_t)(i * 4));
                asic_request_domain_counters((uint8_t)(i * 4));
            }
            continue;
        }

        /* Request register reads - hashrate computed via callback when response arrives */
        for (int i = 0; i < s_info.chip_count; i++) {
            asic_request_hash_counter((uint8_t)(i * 4));
            asic_request_domain_counters((uint8_t)(i * 4));
            asic_request_error_counters((uint8_t)(i * 4));
        }

        /* Small delay for responses, then aggregate */
        vTaskDelay(pdMS_TO_TICKS(100));

        /* Read computed hashrates from the measurement callbacks */
        float total_ghs = 0.0f;
        for (int i = 0; i < s_info.chip_count; i++) {
            float chip_ghs = asic_get_chip_hashrate(i);
            s_info.per_chip_hashrate_ghs[i] = chip_ghs;
            total_ghs += chip_ghs;

            for (int d = 0; d < HASHRATE_NUM_DOMAINS; d++) {
                s_info.per_domain_hashrate_ghs[i][d] = asic_get_domain_hashrate(i, d);
            }
        }

        /* EMA smoothing on total */
        if (total_ghs > 0.0f) {
            if (s_info.total_hashrate_ghs == 0.0f) {
                s_info.total_hashrate_ghs = total_ghs;
            } else {
                s_info.total_hashrate_ghs =
                    (s_info.total_hashrate_ghs * (EMA_ALPHA - 1) + total_ghs) / EMA_ALPHA;
            }
        }

        ESP_LOGI(TAG, "Hashrate: %.2f GH/s (%d chips)",
                 s_info.total_hashrate_ghs, s_info.chip_count);
    }
}

void hashrate_task_start(void)
{
    memset(&s_info, 0, sizeof(s_info));

    xTaskCreate(hashrate_task_fn, "hashrate", HASHRATE_TASK_STACK_SIZE,
                NULL, HASHRATE_TASK_PRIORITY, NULL);
}
