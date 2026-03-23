#include "serial.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "serial";

static uart_port_t s_port;
static SemaphoreHandle_t s_tx_mutex;

#define SERIAL_RX_BUF_SIZE 1024
#define SERIAL_TX_BUF_SIZE 0
#define MUTEX_TIMEOUT_MS   1000

esp_err_t serial_init(const serial_config_t *config)
{
    s_port = config->port;

    const uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_param_config(s_port, &uart_config);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_set_pin(s_port, config->tx_pin, config->rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        return err;
    }

    err = uart_driver_install(s_port, SERIAL_RX_BUF_SIZE, SERIAL_TX_BUF_SIZE,
                              0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }

    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART%d initialised: baud=%d tx=%d rx=%d",
             s_port, config->baud_rate, config->tx_pin, config->rx_pin);

    return ESP_OK;
}

int serial_tx(const uint8_t *data, size_t len)
{
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return -1;
    }

    int written = uart_write_bytes(s_port, data, len);

    xSemaphoreGive(s_tx_mutex);
    return written;
}

int serial_rx(uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    int read = uart_read_bytes(s_port, buf, len, pdMS_TO_TICKS(timeout_ms));
    return read;
}

esp_err_t serial_set_baud(int baud_rate)
{
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t err = uart_set_baudrate(s_port, baud_rate);

    xSemaphoreGive(s_tx_mutex);
    return err;
}

void serial_flush_rx(void)
{
    uart_flush_input(s_port);
}
