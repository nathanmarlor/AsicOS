#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"
#include "self_test.h"

static const char *TAG = "asicos";

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");

    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board: %s, ASIC: %s", board->name, board->asic_model);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    selftest_report_t report = selftest_run();
    for (int i = 0; i < report.check_count; i++) {
        selftest_check_t check = report.checks[i];
        const char *result_str = (check.result == SELFTEST_PASS) ? "PASS" :
                                 (check.result == SELFTEST_FAIL) ? "FAIL" :
                                 (check.result == SELFTEST_WARN) ? "WARN" : "SKIP";
        ESP_LOGI(TAG, "POST [%s] %s: %s", result_str, check.name, check.detail);
    }
    if (!report.all_pass) {
        ESP_LOGW(TAG, "POST: some checks failed, continuing boot");
    }
}
