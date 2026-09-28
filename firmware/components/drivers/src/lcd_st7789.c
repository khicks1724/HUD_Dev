#include "lcd_st7789.h"

#include <string.h>

#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "lcd";

#define BOUNCE_LINES 24

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_free; /* counts free bounce buffers */
static uint16_t *s_bounce[2];
static lcd_cfg_t s_cfg;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    (void)ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_free, &woken);
    return woken == pdTRUE;
}

static void backlight_init(gpio_num_t pin)
{
    const ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    const ledc_channel_config_t c = {
        .gpio_num = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void lcd_set_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)(1023 * percent / 100));
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t lcd_set_mirror(bool mx, bool my)
{
    s_cfg.mirror_x = mx;
    s_cfg.mirror_y = my;
    /* Mirroring in Y on a 240x240 window of a 240x320 controller moves the
     * visible area, so the gap must follow. */
    esp_lcd_panel_set_gap(s_panel, s_cfg.gap_x, my ? 320 - s_cfg.height - s_cfg.gap_y : s_cfg.gap_y);
    return esp_lcd_panel_mirror(s_panel, mx, my);
}

esp_err_t lcd_init(const lcd_cfg_t *cfg)
{
    s_cfg = *cfg;
    const size_t chunk_bytes = (size_t)cfg->width * BOUNCE_LINES * sizeof(uint16_t);

    const spi_bus_config_t bus = {
        .mosi_io_num = cfg->mosi,
        .miso_io_num = -1,
        .sclk_io_num = cfg->sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int)chunk_bytes,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(cfg->host, &bus, SPI_DMA_CH_AUTO));

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = cfg->dc,
        .cs_gpio_num = cfg->cs,
        .pclk_hz = cfg->pclk_hz,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
        .on_color_trans_done = on_color_done,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)cfg->host, &io_cfg, &s_io));

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = cfg->rst,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, cfg->invert_colors));
    ESP_ERROR_CHECK(lcd_set_mirror(cfg->mirror_x, cfg->mirror_y));

    for (int i = 0; i < 2; i++) {
        s_bounce[i] = heap_caps_malloc(chunk_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_bounce[i]) {
            ESP_LOGE(TAG, "bounce buffer alloc failed");
            return ESP_ERR_NO_MEM;
        }
    }
    s_free = xSemaphoreCreateCounting(2, 2);

    /* Clear to black before the backlight comes on (black = invisible in the prism). */
    memset(s_bounce[0], 0, chunk_bytes);
    for (int y = 0; y < cfg->height; y += BOUNCE_LINES) {
        xSemaphoreTake(s_free, portMAX_DELAY);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, cfg->width, y + BOUNCE_LINES, s_bounce[0]);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    backlight_init(cfg->bl);
    lcd_set_backlight(80);
    ESP_LOGI(TAG, "ST7789 %dx%d @ %d MHz, mirror x=%d y=%d", cfg->width, cfg->height, cfg->pclk_hz / 1000000,
             cfg->mirror_x, cfg->mirror_y);
    return ESP_OK;
}

esp_err_t lcd_push_frame(const uint16_t *fb)
{
    int k = 0;
    for (int y = 0; y < s_cfg.height; y += BOUNCE_LINES) {
        const int lines = (y + BOUNCE_LINES <= s_cfg.height) ? BOUNCE_LINES : s_cfg.height - y;
        xSemaphoreTake(s_free, portMAX_DELAY);
        memcpy(s_bounce[k], fb + (size_t)y * s_cfg.width, (size_t)lines * s_cfg.width * sizeof(uint16_t));
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, s_cfg.width, y + lines, s_bounce[k]);
        if (err != ESP_OK) {
            xSemaphoreGive(s_free);
            return err;
        }
        k ^= 1;
    }
    return ESP_OK;
}
