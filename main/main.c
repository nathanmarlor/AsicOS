#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "board.h"

static const char *TAG = "asicos";

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");

    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board: %s, ASIC: %s", board->name, board->asic_model);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
}
