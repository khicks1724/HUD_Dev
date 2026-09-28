/*
 * lcd_st7789.h - ST7789 240x240 panel via esp_lcd, fed from a PSRAM
 * framebuffer through small internal-RAM DMA bounce buffers.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_host_device_t host;
    gpio_num_t mosi, sclk, cs, dc, rst, bl;
    int width, height;
    int pclk_hz;        /* 40-80 MHz; 60 MHz is a safe default for ST7789 */
    bool mirror_x;      /* prism reflection flips the image */
    bool mirror_y;
    bool invert_colors; /* this panel needs inversion on */
    int gap_x, gap_y;   /* controller RAM offset for 240x240 glass on a 240x320 controller */
} lcd_cfg_t;

esp_err_t lcd_init(const lcd_cfg_t *cfg);

/* Push a full width*height RGB565 (big-endian) frame. Blocks until queued. */
esp_err_t lcd_push_frame(const uint16_t *fb);

/* 0..100 % */
void lcd_set_backlight(int percent);

/* Change mirroring at runtime (e.g. when switching prism / direct view). */
esp_err_t lcd_set_mirror(bool mirror_x, bool mirror_y);

#ifdef __cplusplus
}
#endif
