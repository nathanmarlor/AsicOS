#include "hashrate_task.h"
#include "api_history.h"
#include "asic.h"
#include "board.h"
#include "power_task.h"
#include "nvs_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "hashrate_task";

#define POLL_INTERVAL_MS        5000
#define WARMUP_MS               10000
#define EMA_ALPHA               12
#define STARTUP_TEST_DELAY_MS   30000
#define STARTUP_TEST_THRESHOLD  0.70f

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
        vTaskDelay(pdMS_TO_TICKS(2));
        asic_request_domain_counters((uint8_t)(i * 4));
        vTaskDelay(pdMS_TO_TICKS(5));
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

        /* Request register reads with small delays between each to avoid
         * overwhelming the ASIC UART and losing responses (matches ForgeOS). */
        for (int i = 0; i < s_info.chip_count; i++) {
            asic_request_hash_counter((uint8_t)(i * 4));
            vTaskDelay(pdMS_TO_TICKS(2));
            asic_request_domain_counters((uint8_t)(i * 4));
            vTaskDelay(pdMS_TO_TICKS(5));
            asic_request_error_counters((uint8_t)(i * 4));
            vTaskDelay(pdMS_TO_TICKS(2));
        }

        /* Wait for all responses to arrive */
        vTaskDelay(pdMS_TO_TICKS(200));

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

        /* One-time startup hashrate validation after mining stabilizes */
        static bool s_startup_test_done = false;
        if (!s_startup_test_done && s_info.total_hashrate_ghs > 0.0f) {
            int64_t uptime_ms = esp_timer_get_time() / 1000;
            if (uptime_ms > STARTUP_TEST_DELAY_MS) {
                uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
                float expected = (float)freq * board->small_core_count
                               * board->expected_chip_count / 1000.0f;
                float pct = s_info.total_hashrate_ghs / expected;
                if (pct < STARTUP_TEST_THRESHOLD) {
                    ESP_LOGW(TAG, "HASHRATE TEST: %.1f GH/s is only %.0f%% of expected %.1f GH/s",
                             s_info.total_hashrate_ghs, pct * 100.0f, expected);
                } else {
                    ESP_LOGI(TAG, "HASHRATE TEST: PASS (%.1f GH/s = %.0f%% of expected %.1f GH/s)",
                             s_info.total_hashrate_ghs, pct * 100.0f, expected);
                }
                s_startup_test_done = true;
            }
        }

        /* Record history for charts */
        const power_status_t *pw = power_task_get_status();
        float eff_jth = (s_info.total_hashrate_ghs > 0 && pw && pw->power_w > 0)
            ? pw->power_w / (s_info.total_hashrate_ghs / 1000.0f) : 0.0f;
        history_record(
            s_info.total_hashrate_ghs,
            pw ? pw->chip_temp : 0.0f,
            pw ? pw->vr_temp : 0.0f,
            pw ? pw->power_w : 0.0f,
            eff_jth
        );

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
