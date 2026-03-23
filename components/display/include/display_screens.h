#pragma once
#include <stdint.h>
#include "display.h"

/* ── RGB565 color definitions ──────────────────────────────────────── */

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_ORANGE  0xFD20
#define COLOR_GRAY    0x7BEF
#define COLOR_DKGRAY  0x39E7

/* ── Drawing primitives (implemented in display.c) ─────────────────── */

void display_fill_screen(uint16_t color);
void display_draw_rect(int x, int y, int w, int h, uint16_t color);
void display_draw_hline(int x, int y, int w, uint16_t color);
void display_draw_char(int x, int y, char c, uint16_t color, uint16_t bg);
void display_draw_text(int x, int y, const char *text, uint16_t color, uint16_t bg);
void display_flush(void);
