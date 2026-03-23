#include "i2c_mux.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "i2c_mux";

#define I2C_TIMEOUT_MS  100
#define PAC9544_REG     0x00

esp_err_t i2c_mux_init(i2c_port_t port, uint8_t mux_addr)
{
    ESP_LOGI(TAG, "PAC9544 mux init at 0x%02X on port %d", mux_addr, port);

    /* Disable all channels on init */
    uint8_t buf[2] = { PAC9544_REG, 0x00 };
    esp_err_t err = i2c_master_write_to_device(port, mux_addr,
                                                buf, sizeof(buf),
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PAC9544 init failed: %s", esp_err_to_name(err));
    }
    return err;
}

static int8_t s_current_channel = -1;

esp_err_t i2c_mux_select(i2c_port_t port, uint8_t mux_addr, uint8_t channel)
{
    if (channel > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Skip I2C write if channel already selected */
    if ((int8_t)channel == s_current_channel) {
        return ESP_OK;
    }

    uint8_t buf[2] = { PAC9544_REG, 0x04 | channel };
    esp_err_t err = i2c_master_write_to_device(port, mux_addr,
                                                buf, sizeof(buf),
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PAC9544 select channel %d failed: %s", channel, esp_err_to_name(err));
        s_current_channel = -1;
        return err;
    }

    s_current_channel = (int8_t)channel;

    /* Allow mux to settle */
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}
