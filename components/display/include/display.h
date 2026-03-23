#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    // Parallel bus pins
    int data_gpio[8];
    int wr_gpio;
    int rd_gpio;
    int cs_gpio;
    int dc_gpio;
    int rst_gpio;
    int backlight_gpio;
    int power_gpio;
    // Buttons
    int button1_gpio;
    int button2_gpio;
    // Display size
    uint16_t width;
    uint16_t height;
} display_config_t;

typedef enum {
    DISPLAY_SCREEN_MINING,
    DISPLAY_SCREEN_STATS,
    DISPLAY_SCREEN_NETWORK,
    DISPLAY_SCREEN_OFF,
} display_screen_t;

esp_err_t display_init(const display_config_t *config);
void display_task_start(void);
void display_set_screen(display_screen_t screen);
void display_set_backlight(bool on);
