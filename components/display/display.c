#include "display.h"
#include "display_screens.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

#include <string.h>

static const char *TAG = "display";

/* ── Display state ─────────────────────────────────────────────────── */

static display_config_t s_config;
static esp_lcd_panel_handle_t s_panel = NULL;
static display_screen_t s_current_screen = DISPLAY_SCREEN_MINING;
static display_screen_cb_t s_render_cb = NULL;
static bool s_backlight_on = true;
static TickType_t s_last_button_tick = 0;

#define DISPLAY_TASK_STACK_SIZE 8192
#define DISPLAY_TASK_PRIORITY   3
#define BACKLIGHT_TIMEOUT_MS    60000
#define DEBOUNCE_MS             200
#define LONG_PRESS_MS           3000
#define SCREEN_UPDATE_MS        2000

/* ── Framebuffer ───────────────────────────────────────────────────── */

// Transfer band buffer (DMA-friendly, internal SRAM)
#define BAND_HEIGHT  10
static uint16_t s_band_buf[320 * BAND_HEIGHT];

// Full framebuffer in PSRAM (ESP32-S3 has PSRAM on NerdQAxe++)
static uint16_t *s_framebuffer = NULL;

/* ── 8x16 Bitmap Font (CP437 subset, ASCII 0x20-0x7E) ─────────────── */

#include "font_8x16.h"

/* ── Drawing primitives ────────────────────────────────────────────── */

void display_fill_screen(uint16_t color)
{
    if (!s_framebuffer) return;
    for (int i = 0; i < s_config.width * s_config.height; i++) {
        s_framebuffer[i] = color;
    }
}

void display_draw_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!s_framebuffer) return;
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= s_config.height) continue;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= s_config.width) continue;
            s_framebuffer[py * s_config.width + px] = color;
        }
    }
}

void display_draw_hline(int x, int y, int w, uint16_t color)
{
    if (!s_framebuffer) return;
    if (y < 0 || y >= s_config.height) return;
    for (int dx = 0; dx < w; dx++) {
        int px = x + dx;
        if (px >= 0 && px < s_config.width) {
            s_framebuffer[y * s_config.width + px] = color;
        }
    }
}

void display_draw_char(int x, int y, char c, uint16_t color, uint16_t bg)
{
    if (!s_framebuffer) return;
    if (c < 0x20 || c > 0x7E) c = '?';

    int idx = c - 0x20;
    const uint8_t *glyph = &font_8x16[idx * 16];

    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= s_config.height) continue;
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= s_config.width) continue;
            uint16_t pixel = (bits & (0x80 >> col)) ? color : bg;
            s_framebuffer[py * s_config.width + px] = pixel;
        }
    }
}

void display_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg)
{
    if (!text) return;
    int cx = x;
    while (*text) {
        if (*text == '\n') {
            y += 16;
            cx = x;
            text++;
            continue;
        }
        display_draw_char(cx, y, *text, color, bg);
        cx += 8;
        text++;
    }
}

/* ── Flush framebuffer to panel ────────────────────────────────────── */

void display_flush(void)
{
    if (!s_panel || !s_framebuffer) return;

    // Draw in bands to avoid overwhelming the bus
    for (int y = 0; y < s_config.height; y += BAND_HEIGHT) {
        int band_h = BAND_HEIGHT;
        if (y + band_h > s_config.height) {
            band_h = s_config.height - y;
        }
        memcpy(s_band_buf, &s_framebuffer[y * s_config.width],
               s_config.width * band_h * sizeof(uint16_t));
        esp_lcd_panel_draw_bitmap(s_panel, 0, y,
                                  s_config.width, y + band_h, s_band_buf);
    }
}

/* ── Backlight control ─────────────────────────────────────────────── */

void display_set_backlight(bool on)
{
    s_backlight_on = on;
    gpio_set_level(s_config.backlight_gpio, on ? 1 : 0);
}

/* ── Screen selection ──────────────────────────────────────────────── */

void display_set_screen(display_screen_t screen)
{
    s_current_screen = screen;
}

display_screen_t display_get_screen(void)
{
    return s_current_screen;
}

/* ── Panel init ────────────────────────────────────────────────────── */

esp_err_t display_init(const display_config_t *config)
{
    memcpy(&s_config, config, sizeof(display_config_t));

    // Allocate framebuffer (try PSRAM first, then internal)
    s_framebuffer = heap_caps_malloc(config->width * config->height * sizeof(uint16_t),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer) {
        s_framebuffer = heap_caps_malloc(config->width * config->height * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%d bytes)",
                 config->width * config->height * 2);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Framebuffer allocated: %dx%d (%d bytes)",
             config->width, config->height,
             config->width * config->height * 2);

    // 1. Power on the display
    if (config->power_gpio >= 0) {
        gpio_config_t pwr_cfg = {
            .pin_bit_mask = (1ULL << config->power_gpio),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&pwr_cfg);
        gpio_set_level(config->power_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 2. Configure backlight GPIO
    if (config->backlight_gpio >= 0) {
        gpio_config_t bl_cfg = {
            .pin_bit_mask = (1ULL << config->backlight_gpio),
            .mode = GPIO_MODE_OUTPUT,
        };
        gpio_config(&bl_cfg);
        gpio_set_level(config->backlight_gpio, 0); // off during init
    }

    // 3. Configure Intel 8080 bus
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = config->dc_gpio,
        .wr_gpio_num = config->wr_gpio,
        .data_gpio_nums = {
            config->data_gpio[0], config->data_gpio[1],
            config->data_gpio[2], config->data_gpio[3],
            config->data_gpio[4], config->data_gpio[5],
            config->data_gpio[6], config->data_gpio[7],
        },
        .bus_width = 8,
        .max_transfer_bytes = config->width * BAND_HEIGHT * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };

    esp_err_t ret = esp_lcd_new_i80_bus(&bus_config, &i80_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I80 bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Create panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = config->cs_gpio,
        .pclk_hz = 10 * 1000 * 1000, // 10 MHz pixel clock
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    ret = esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        return ret;
    }

    // 5. Create ST7789 panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = config->rst_gpio,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };

    ret = esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7789 panel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 6. Reset and init the panel
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    // 7. Configure for landscape 320x170
    // The ST7789 is natively 240x320; for 170x320 we need an offset
    // Mirror X and swap X/Y for landscape orientation
    esp_lcd_panel_mirror(s_panel, true, true);
    esp_lcd_panel_swap_xy(s_panel, false);

    // Set the display window offset for 170-pixel height (centered in 240)
    // Offset = (240 - 170) / 2 = 35
    esp_lcd_panel_set_gap(s_panel, 0, 35);

    // Invert colors (ST7789 typically needs this)
    esp_lcd_panel_invert_color(s_panel, true);

    // Turn on display
    esp_lcd_panel_disp_on_off(s_panel, true);

    // 8. Configure button GPIOs
    if (config->button1_gpio >= 0) {
        gpio_config_t btn_cfg = {
            .pin_bit_mask = (1ULL << config->button1_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&btn_cfg);
    }
    if (config->button2_gpio >= 0) {
        gpio_config_t btn_cfg = {
            .pin_bit_mask = (1ULL << config->button2_gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
        };
        gpio_config(&btn_cfg);
    }

    // 9. Clear screen to black
    display_fill_screen(COLOR_BLACK);
    display_flush();

    // 10. Turn on backlight
    if (config->backlight_gpio >= 0) {
        gpio_set_level(config->backlight_gpio, 1);
    }
    s_backlight_on = true;
    s_last_button_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "Display initialized: %dx%d ST7789 via I80 bus", config->width, config->height);
    return ESP_OK;
}

/* ── Display task ──────────────────────────────────────────────────── */

static void display_task(void *param)
{
    TickType_t last_update = 0;
    bool btn1_was_pressed = false;
    bool btn2_was_pressed = false;
    TickType_t btn1_press_start = 0;
    TickType_t btn2_press_start = 0;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        // Read button states (active low)
        bool btn1_pressed = (s_config.button1_gpio >= 0) &&
                            (gpio_get_level(s_config.button1_gpio) == 0);
        bool btn2_pressed = (s_config.button2_gpio >= 0) &&
                            (gpio_get_level(s_config.button2_gpio) == 0);

        // Track press start times
        if (btn1_pressed && !btn1_was_pressed) {
            btn1_press_start = now;
        }
        if (btn2_pressed && !btn2_was_pressed) {
            btn2_press_start = now;
        }

        // Long press both buttons (3s): toggle backlight
        if (btn1_pressed && btn2_pressed) {
            TickType_t hold_time = (now - btn1_press_start) * portTICK_PERIOD_MS;
            if (hold_time >= LONG_PRESS_MS) {
                display_set_backlight(!s_backlight_on);
                s_last_button_tick = now;
                // Wait for release
                while (gpio_get_level(s_config.button1_gpio) == 0 ||
                       gpio_get_level(s_config.button2_gpio) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                btn1_was_pressed = false;
                btn2_was_pressed = false;
                continue;
            }
        }

        // Button 1 short press: cycle screens (on release)
        if (!btn1_pressed && btn1_was_pressed && !btn2_pressed) {
            TickType_t hold_ms = (now - btn1_press_start) * portTICK_PERIOD_MS;
            if (hold_ms < LONG_PRESS_MS && hold_ms >= DEBOUNCE_MS) {
                s_last_button_tick = now;

                // Wake backlight if off
                if (!s_backlight_on) {
                    display_set_backlight(true);
                } else {
                    // Cycle screen
                    switch (s_current_screen) {
                        case DISPLAY_SCREEN_MINING:
                            s_current_screen = DISPLAY_SCREEN_STATS;
                            break;
                        case DISPLAY_SCREEN_STATS:
                            s_current_screen = DISPLAY_SCREEN_NETWORK;
                            break;
                        case DISPLAY_SCREEN_NETWORK:
                        default:
                            s_current_screen = DISPLAY_SCREEN_MINING;
                            break;
                    }
                }
                // Force immediate screen update
                last_update = 0;
            }
        }

        btn1_was_pressed = btn1_pressed;
        btn2_was_pressed = btn2_pressed;

        // Auto-off backlight after timeout
        if (s_backlight_on &&
            ((now - s_last_button_tick) * portTICK_PERIOD_MS >= BACKLIGHT_TIMEOUT_MS)) {
            display_set_backlight(false);
        }

        // Update screen every SCREEN_UPDATE_MS
        if ((now - last_update) * portTICK_PERIOD_MS >= SCREEN_UPDATE_MS) {
            last_update = now;

            display_fill_screen(COLOR_BLACK);

            if (s_render_cb) {
                s_render_cb(s_current_screen);
            }

            display_flush();
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void display_task_start(display_screen_cb_t render_cb)
{
    s_render_cb = render_cb;
    xTaskCreate(display_task, "display", DISPLAY_TASK_STACK_SIZE, NULL,
                DISPLAY_TASK_PRIORITY, NULL);
    ESP_LOGI(TAG, "Display task started");
}
