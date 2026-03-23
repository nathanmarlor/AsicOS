#include "led_status.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "led";

static int s_led1_gpio = -1;
static int s_led2_gpio = -1;
static led_state_t s_state = LED_STATE_OFF;
static esp_timer_handle_t s_timer;
static uint32_t s_tick = 0;         /* Increments every 100ms */
static volatile int s_flash_ticks = 0;  /* Countdown for share flash */

/* LEDs are active-LOW on the BitForge Nano */
#define LED_ON   0
#define LED_OFF  1

static void led_set(int gpio, int level)
{
    if (gpio >= 0) {
        gpio_set_level(gpio, level);
    }
}

static void led_timer_cb(void *arg)
{
    (void)arg;
    s_tick++;

    switch (s_state) {
    case LED_STATE_OFF:
        led_set(s_led1_gpio, LED_OFF);
        led_set(s_led2_gpio, LED_OFF);
        break;

    case LED_STATE_BOOTING:
        /* Slow blink: toggle every 500ms (5 ticks) */
        led_set(s_led1_gpio, (s_tick / 5) % 2 ? LED_ON : LED_OFF);
        led_set(s_led2_gpio, LED_OFF);
        break;

    case LED_STATE_WIFI_AP:
        /* Fast blink: toggle every 200ms (2 ticks) */
        led_set(s_led1_gpio, (s_tick / 2) % 2 ? LED_ON : LED_OFF);
        led_set(s_led2_gpio, LED_OFF);
        break;

    case LED_STATE_CONNECTED:
        /* Solid on */
        led_set(s_led1_gpio, LED_ON);
        led_set(s_led2_gpio, LED_OFF);
        break;

    case LED_STATE_MINING:
        /* LED off normally, flashes on share submission via led_flash() */
        if (s_flash_ticks > 0) {
            led_set(s_led1_gpio, LED_ON);
            s_flash_ticks--;
        } else {
            led_set(s_led1_gpio, LED_OFF);
        }
        led_set(s_led2_gpio, LED_OFF);
        break;

    case LED_STATE_ERROR:
        /* Rapid blink: both LEDs toggle every 100ms (every tick) */
        led_set(s_led1_gpio, (s_tick % 2) ? LED_ON : LED_OFF);
        led_set(s_led2_gpio, (s_tick % 2) ? LED_ON : LED_OFF);
        break;
    }
}

esp_err_t led_init(int led1_gpio, int led2_gpio)
{
    s_led1_gpio = led1_gpio;
    s_led2_gpio = led2_gpio;

    /* Configure LED GPIOs as outputs, start OFF (HIGH for active-LOW) */
    if (led1_gpio >= 0) {
        gpio_reset_pin(led1_gpio);
        gpio_set_direction(led1_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(led1_gpio, LED_OFF);
    }
    if (led2_gpio >= 0) {
        gpio_reset_pin(led2_gpio);
        gpio_set_direction(led2_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(led2_gpio, LED_OFF);
    }

    /* Create periodic timer at 100ms interval */
    esp_timer_create_args_t timer_args = {
        .callback = led_timer_cb,
        .name = "led_timer",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED timer: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(s_timer, 100 * 1000);  /* 100ms in microseconds */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LED timer: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LED status initialized (gpio1=%d, gpio2=%d)", led1_gpio, led2_gpio);
    return ESP_OK;
}

void led_set_state(led_state_t state)
{
    s_state = state;
    s_tick = 0;  /* Reset tick counter on state change for clean pattern start */
}

void led_flash(void)
{
    /* Brief flash: 2 ticks = 200ms on. Called from result_task on share submission. */
    s_flash_ticks = 2;
}
