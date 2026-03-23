#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"

#include "nvs_config.h"
#include "board.h"
#include "self_test.h"
#include "serial.h"
#include "bm1370.h"
#include "asic.h"
#include "voltage_regulator.h"
#include "temp_sensor.h"
#include "fan_controller.h"
#include "stratum_client.h"
#include "http_server.h"
#include "remote_access.h"
#include "licence.h"

#include "tasks/wifi_task.h"
#include "tasks/mining_task.h"
#include "tasks/result_task.h"
#include "tasks/hashrate_task.h"
#include "tasks/power_task.h"
#include "tasks/tuner_task.h"
#include "tasks/remote_task.h"
#include "tasks/loki_task.h"

static const char *TAG = "asicos";

static esp_err_t init_i2c(const board_config_t *board)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = board->i2c_sda_pin,
        .scl_io_num = board->i2c_scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

static void init_stratum(const board_config_t *board)
{
    stratum_client_config_t cfg = {0};
    nvs_config_get_string(NVS_KEY_POOL_URL, cfg.primary.pool_url, sizeof(cfg.primary.pool_url), "public-pool.io");
    cfg.primary.pool_port = nvs_config_get_u16(NVS_KEY_POOL_PORT, DEFAULT_POOL_PORT);
    nvs_config_get_string(NVS_KEY_POOL_USER, cfg.primary.pool_user, sizeof(cfg.primary.pool_user), "");
    nvs_config_get_string(NVS_KEY_POOL_PASS, cfg.primary.pool_pass, sizeof(cfg.primary.pool_pass), "x");

    // Fallback pool
    nvs_config_get_string(NVS_KEY_POOL2_URL, cfg.fallback.pool_url, sizeof(cfg.fallback.pool_url), "");
    cfg.fallback.pool_port = nvs_config_get_u16(NVS_KEY_POOL2_PORT, DEFAULT_POOL_PORT);
    nvs_config_get_string(NVS_KEY_POOL2_USER, cfg.fallback.pool_user, sizeof(cfg.fallback.pool_user), "");

    cfg.on_notify = mining_on_notify;
    cfg.on_difficulty = mining_on_difficulty;

    stratum_client_init(&cfg);
    xTaskCreate(stratum_client_task, "stratum", 8192, NULL, 5, NULL);
}

static void init_power(const board_config_t *board)
{
    vr_config_t vr_cfg = {
        .port = I2C_NUM_0,
        .address = board->vr_i2c_addr,
    };
    vr_init(&vr_cfg);

    uint16_t voltage = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, board->voltage_default);
    vr_set_voltage(voltage);
    vr_enable(true);

    temp_sensor_config_t temp_cfg = {
        .port = I2C_NUM_0,
        .count = board->temp_sensor_count,
    };
    // Set default TMP1075 addresses
    for (int i = 0; i < temp_cfg.count && i < TEMP_SENSOR_MAX_COUNT; i++) {
        temp_cfg.addresses[i] = 0x48 + i;
    }
    temp_sensor_init(&temp_cfg);

    fan_config_t fan_cfg = {
        .port = I2C_NUM_0,
        .address = board->fan_i2c_addr,
    };
    fan_init(&fan_cfg);
}

static void init_remote(void)
{
    char device_id[32];
    licence_get_device_id(device_id, sizeof(device_id));

    remote_config_t remote_cfg = {
        .relay_host = "relay.asicos.io",
        .relay_port = 8883,
        .device_id = device_id,
    };
    remote_init(&remote_cfg);
    remote_task_start();
}

void app_main(void)
{
    ESP_LOGI(TAG, "AsicOS starting...");

    // 1. NVS init
    nvs_config_init();

    // 2. Board config
    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board: %s, ASIC: %s", board->name, board->asic_model);

    // 3. Self-test (POST)
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

    // 4. WiFi (non-blocking, runs as FreeRTOS task)
    wifi_task_start();

    // 5. I2C bus init (needed before power/fan/temp)
    esp_err_t i2c_err = init_i2c(board);
    if (i2c_err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(i2c_err));
    }

    // 6. Serial/UART
    serial_config_t serial_cfg = {
        .port = UART_NUM_1,
        .tx_pin = board->uart_tx_pin,
        .rx_pin = board->uart_rx_pin,
        .baud_rate = 115200,
    };
    serial_init(&serial_cfg);

    // 7. ASIC init
    uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
    bm1370_init(board->expected_chip_count);
    bm1370_set_frequency(freq);
    serial_set_baud(1000000);

    // 8. Power subsystem init (voltage regulator, temp sensors, fan)
    init_power(board);

    // 9. HTTP server
    http_server_start();

    // 10. Stratum client
    init_stratum(board);

    // 11. Mining tasks
    mining_task_start();
    result_task_start();
    hashrate_task_start();

    // 12. Power task
    power_task_start();

    // 13. Tuner task (idle until triggered)
    tuner_task_start();

    // 14. Remote access
    init_remote();

    // 15. Loki task
    loki_task_start();

    // 16. Log completion
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "All systems started");
}
