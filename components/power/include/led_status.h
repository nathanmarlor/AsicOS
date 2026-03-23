#pragma once
#include "esp_err.h"

typedef enum {
    LED_STATE_OFF,
    LED_STATE_BOOTING,      // Slow blink
    LED_STATE_WIFI_AP,      // Fast blink
    LED_STATE_CONNECTED,    // Solid on
    LED_STATE_MINING,       // Off normally, flashes on share submission
    LED_STATE_ERROR,        // Rapid blink
} led_state_t;

esp_err_t led_init(int led1_gpio, int led2_gpio);
void led_set_state(led_state_t state);
void led_flash(void);  // Brief 200ms flash (call on share submit)
