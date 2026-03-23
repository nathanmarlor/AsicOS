#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

#include "nvs_config.h"
#include "board.h"
#include "self_test.h"
#include "serial.h"
#include "bm1370.h"
#include "asic.h"
#include "voltage_regulator.h"
#include "temp_sensor.h"
#include "fan_controller.h"
#include "ina260.h"
#include "i2c_mux.h"
#include "stratum_client.h"
#include "http_server.h"
#include "remote_access.h"
#include "licence.h"

#include "adc_monitor.h"
#include "led_status.h"
#include "plug_sense.h"

#include "tasks/wifi_task.h"
#include "tasks/mining_task.h"
#include "tasks/result_task.h"
#include "tasks/hashrate_task.h"
#include "tasks/power_task.h"
#include "tasks/tuner_task.h"
#include "tasks/remote_task.h"
#include "tasks/loki_task.h"
#include "display.h"
#include "http_server/ws_handler.h"

// Defined in display_screens.c
extern void display_render_screen(display_screen_t screen);

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
    xTaskCreate(stratum_client_task, "stratum", 24576, NULL, 5, NULL);
}

static void init_power(const board_config_t *board)
{
    vr_config_t vr_cfg = {
        .port = I2C_NUM_0,
        .address = board->vr_i2c_addr,
        .type = board->vr_type,
    };
    vr_init(&vr_cfg);

    uint16_t voltage = nvs_config_get_u16(NVS_KEY_ASIC_VOLTAGE, board->voltage_default);
    vr_set_voltage(voltage);
    vr_enable(true);

    temp_sensor_config_t temp_cfg = {
        .port = I2C_NUM_0,
        .count = board->temp_sensor_count,
        .type = board->temp_type,
    };
    if (board->temp_type == TEMP_TYPE_EMC2101) {
        /* EMC2101 sensors behind PAC9544 mux */
        temp_cfg.mux_addr = board->fan_i2c_addr; /* mux address (0x70) */
        for (int i = 0; i < temp_cfg.count && i < TEMP_SENSOR_MAX_COUNT; i++) {
            temp_cfg.addresses[i] = 0x4C;       /* EMC2101 device address */
            temp_cfg.mux_channels[i] = 2 + i;   /* channels 2, 3 */
        }
    } else {
        /* TMP1075 addresses */
        for (int i = 0; i < temp_cfg.count && i < TEMP_SENSOR_MAX_COUNT; i++) {
            temp_cfg.addresses[i] = 0x48 + i;
        }
    }
    temp_sensor_init(&temp_cfg);

    fan_config_t fan_cfg = {
        .port = I2C_NUM_0,
        .address = board->fan_i2c_addr,
        .type = board->fan_type,
    };
    if (board->fan_type == FAN_TYPE_EMC2101_MUX) {
        fan_cfg.dev_addr = 0x4C;
        fan_cfg.mux_channels[0] = 2;
        fan_cfg.mux_channels[1] = 3;
    }
    fan_init(&fan_cfg);

    /* INA260 power monitor (if present) */
    if (board->power_monitor_type == 1 && board->power_monitor_addr != 0) {
        ina260_config_t ina_cfg = {
            .port = I2C_NUM_0,
            .address = board->power_monitor_addr,
        };
        ina260_init(&ina_cfg);
    }
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

    // 3. WiFi (non-blocking, runs as FreeRTOS task)
    wifi_task_start();

    // 4. I2C bus init (needed before power/fan/temp)
    esp_err_t i2c_err = init_i2c(board);
    if (i2c_err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(i2c_err));
    }

    // 4b. LED init
    if (board->led1_gpio >= 0) {
        led_init(board->led1_gpio, board->led2_gpio);
        led_set_state(LED_STATE_BOOTING);
    }

    // 4c. ADC init (VCORE readback + thermistors, if board has it)
    if (board->has_adc_vcore) {
        esp_err_t adc_err = adc_monitor_init();
        if (adc_err != ESP_OK) {
            ESP_LOGW(TAG, "ADC monitor init failed: %s", esp_err_to_name(adc_err));
        }
    }

    // 4d. Barrel plug sense
    if (board->plug_sense_gpio >= 0) {
        plug_sense_init(board->plug_sense_gpio);
        if (!plug_sense_is_connected()) {
            ESP_LOGW(TAG, "Barrel plug not detected at boot");
        }
    }

    // 5. Serial/UART init at 115200
    serial_config_t serial_cfg = {
        .port = UART_NUM_1,
        .tx_pin = board->uart_tx_pin,
        .rx_pin = board->uart_rx_pin,
        .baud_rate = 115200,
    };
    serial_init(&serial_cfg);

    // 6. Power subsystem init (VR MUST be initialized BEFORE ASIC reset)
    init_power(board);

    // 7. Wait for voltage to stabilize after VR enable
    ESP_LOGI(TAG, "Waiting for voltage stabilization...");
    vTaskDelay(pdMS_TO_TICKS(500));

    // 8. GPIO power-on sequence
    // Only toggle buck_enable GPIO on boards that use GPIO-controlled power
    // (e.g. NerdQAxe with separate buck + LDO). On Nano (TPS546D24A),
    // power is controlled via PMBus OPERATION register, not GPIO.
    if (board->vr_type != 1 && board->ldo_enable_pin >= 0) {
        gpio_reset_pin(board->buck_enable_pin);
        gpio_set_direction(board->buck_enable_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(board->buck_enable_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Enable LDO if present (some boards like NerdQAxe have separate LDO)
    if (board->ldo_enable_pin >= 0) {
        gpio_reset_pin(board->ldo_enable_pin);
        gpio_set_direction(board->ldo_enable_pin, GPIO_MODE_OUTPUT);
        gpio_set_level(board->ldo_enable_pin, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // ASIC reset sequence: assert low, wait, release high
    gpio_reset_pin(board->asic_reset_pin);
    gpio_set_direction(board->asic_reset_pin, GPIO_MODE_OUTPUT);
    gpio_set_level(board->asic_reset_pin, 0);  // Assert reset
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(board->asic_reset_pin, 1);  // Release reset
    vTaskDelay(pdMS_TO_TICKS(100));

    // 9. ASIC init (enumerate, configure registers)
    uint16_t freq = nvs_config_get_u16(NVS_KEY_ASIC_FREQ, board->freq_default);
    bm1370_init(board->expected_chip_count);
    bm1370_set_frequency(freq);

    // 10. Ramp UART to 1MHz for mining
    // First configure the ASIC's UART divider, THEN switch the host baud
    bm1370_set_max_baud();
    serial_set_baud(1000000);

    // 11. Self-test (POST) - runs AFTER everything is initialized
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

    // 12. HTTP server
    http_server_start();
    // ws_log_init() disabled: overriding esp_log_set_vprintf silences the
    // serial console, making UART0 debugging impossible.  WebSocket log
    // forwarding can be re-enabled once a non-intrusive hook is available.

    // 13. Stratum client
    init_stratum(board);

    // 13b. LED: connected
    if (board->led1_gpio >= 0) {
        led_set_state(LED_STATE_CONNECTED);
    }

    // 14. Mining tasks
    mining_task_start();
    result_task_start();
    hashrate_task_start();

    // 14b. LED: mining active
    if (board->led1_gpio >= 0) {
        led_set_state(LED_STATE_MINING);
    }

    // 15. Power task
    power_task_start();

    // 16. Tuner task (idle until triggered)
    tuner_task_start();

    // 17. Remote access
    init_remote();

    // 18. Loki task
    loki_task_start();

    // 19. Display (if board has one)
    if (board->has_display) {
        display_config_t disp_cfg = {
            .data_gpio = {
                board->display_data_gpio[0], board->display_data_gpio[1],
                board->display_data_gpio[2], board->display_data_gpio[3],
                board->display_data_gpio[4], board->display_data_gpio[5],
                board->display_data_gpio[6], board->display_data_gpio[7],
            },
            .wr_gpio = board->display_wr_gpio,
            .rd_gpio = board->display_rd_gpio,
            .cs_gpio = board->display_cs_gpio,
            .dc_gpio = board->display_dc_gpio,
            .rst_gpio = board->display_rst_gpio,
            .backlight_gpio = board->display_bl_gpio,
            .power_gpio = board->display_pwr_gpio,
            .button1_gpio = board->button1_gpio,
            .button2_gpio = board->button2_gpio,
            .width = board->display_width,
            .height = board->display_height,
        };
        esp_err_t disp_err = display_init(&disp_cfg);
        if (disp_err == ESP_OK) {
            display_task_start(display_render_screen);
        } else {
            ESP_LOGW(TAG, "Display init failed: %s", esp_err_to_name(disp_err));
        }
    }

    // 20. Log completion
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "All systems started");
}
