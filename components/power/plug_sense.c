#include "plug_sense.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "plug_sense";

static int s_gpio = -1;
static bool s_initialized = false;

esp_err_t plug_sense_init(int gpio)
{
    if (gpio < 0) return ESP_ERR_INVALID_ARG;

    s_gpio = gpio;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure plug sense GPIO %d: %s", gpio, esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Barrel plug sense initialized on GPIO %d", gpio);
    return ESP_OK;
}

bool plug_sense_is_connected(void)
{
    if (!s_initialized) return false;
    return gpio_get_level(s_gpio) == 1;  /* HIGH = plugged in */
}
