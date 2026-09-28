/*
 * hub_link.c - HUD side of the V2H hub backpack link
 * (hardware/hub_backpack/README.md).
 *
 *   UART1 (2 Mbit/s): text lines from the hub
 *       fix <lat> <lon> <hae>
 *       trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign...>
 *       hdg <deg> <gain>
 *       thm <0|1|2>
 *       bat ... / cam ...        (status, shown on the STATUS page)
 *     and back to the hub, 5 Hz:  att <hdg> <pitch> <roll> <tracks> <link>
 *
 *   SPI3 slave + READY line: 240x240 8-bit thermal frames in 4 chunks of
 *   60 rows, each with a 16-byte 'THM1' header.
 */
#include "hub_link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_state.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "imu_task.h"
#include "sdkconfig.h"
#include "thermal.h"

#if CONFIG_HUD_HUB

static const char *TAG = "hub";

#define HUB_UART UART_NUM_1
#define HUB_BAUD 2000000
#define TH_W 240
#define TH_H 240
#define CHUNK_ROWS 60
#define HDR_BYTES 16
#define CHUNK_BYTES (HDR_BYTES + TH_W * CHUNK_ROWS) /* 14416, multiple of 4 for DMA */
#define THM_MAGIC 0x314D4854u                      /* 'THM1' little-endian */

/* ------------------------------------------------------------------ UART */

static void handle_line(char *line)
{
    char *argv[16];
    int argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < 16; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (argc == 0) return;

    if (strcmp(argv[0], "fix") == 0 && argc >= 3) {
        app_set_own(atof(argv[1]), atof(argv[2]), argc > 3 ? atof(argv[3]) : 0.0, POS_USB);
    } else if (strcmp(argv[0], "trk") == 0 && argc >= 8) {
        char cs[HUD_COT_CALLSIGN_LEN] = "";
        size_t used = 0;
        for (int i = 7; i < argc && used + 1 < sizeof(cs); i++) {
            const int n = snprintf(cs + used, sizeof(cs) - used, "%s%s", i > 7 ? " " : "", argv[i]);
            if (n < 0) break;
            used += (size_t)n;
        }
        app_ingest_track(argv[1], argv[2], atof(argv[3]), atof(argv[4]), atof(argv[5]), atoi(argv[6]), cs, "hub");
    } else if (strcmp(argv[0], "hdg") == 0 && argc >= 2) {
        const float gain = argc > 2 ? (float)atof(argv[2]) : 0.05f;
        imu_set_heading((float)atof(argv[1]), gain, gain >= 1.0f ? HDG_BORE : HDG_PHONE);
    } else if (strcmp(argv[0], "thm") == 0 && argc >= 2) {
        app_lock();
        g_app.thermal_mode = (hud_thermal_mode_t)(atoi(argv[1]) % HUD_THERMAL_COUNT);
        app_unlock();
    } else if (strcmp(argv[0], "bat") == 0 || strcmp(argv[0], "cam") == 0) {
        app_lock();
        char *p = g_app.hub_status;
        size_t left = sizeof(g_app.hub_status);
        p[0] = '\0';
        for (int i = 0; i < argc && left > 1; i++) {
            const int n = snprintf(p, left, "%s%s", i ? " " : "", argv[i]);
            if (n < 0 || (size_t)n >= left) break;
            p += n;
            left -= (size_t)n;
        }
        app_unlock();
    }
}

static void uart_task(void *arg)
{
    (void)arg;
    static char line[256];
    size_t len = 0;
    uint8_t buf[256];
    int64_t last_tx = 0;
    for (;;) {
        const int n = uart_read_bytes(HUB_UART, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; i++) {
            const char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (len) {
                    line[len] = '\0';
                    handle_line(line);
                    len = 0;
                }
            } else if (len < sizeof(line) - 1) {
                line[len++] = c;
            } else {
                len = 0; /* overlong: drop */
            }
        }
        const int64_t now = app_mono_ms();
        if (now - last_tx >= 200) {
            last_tx = now;
            hud_euler_t e;
            hud_quat_to_euler(imu_get_quat(), &e);
            app_lock();
            const int tracks = g_app.targets->count, link = g_app.link_state;
            app_unlock();
            char out[96];
            const int m = snprintf(out, sizeof(out), "att %.1f %.1f %.1f %d %d\n", e.heading_deg, e.pitch_deg,
                                   e.roll_deg, tracks, link);
            if (m > 0) uart_write_bytes(HUB_UART, out, (size_t)m);
        }
    }
}

/* ------------------------------------------------------------------ SPI thermal */

static void IRAM_ATTR ready_high(spi_slave_transaction_t *t)
{
    (void)t;
    gpio_set_level(HUB_READY, 1);
}

static void IRAM_ATTR ready_low(spi_slave_transaction_t *t)
{
    (void)t;
    gpio_set_level(HUB_READY, 0);
}

static void spi_task(void *arg)
{
    (void)arg;
    uint8_t *rx = heap_caps_malloc(CHUNK_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint8_t *frames[2] = {
        heap_caps_calloc(TH_W * TH_H, 1, MALLOC_CAP_SPIRAM),
        heap_caps_calloc(TH_W * TH_H, 1, MALLOC_CAP_SPIRAM),
    };
    if (!rx || !frames[0] || !frames[1]) {
        ESP_LOGE(TAG, "thermal buffers: out of memory");
        vTaskDelete(NULL);
        return;
    }
    int back = 0;
    uint16_t cur_id = 0;
    uint8_t rows_mask = 0;
    uint32_t frames_ok = 0, bad = 0;

    for (;;) {
        spi_slave_transaction_t t = {.length = CHUNK_BYTES * 8, .rx_buffer = rx};
        if (spi_slave_transmit(SPI3_HOST, &t, portMAX_DELAY) != ESP_OK) continue;
        if (t.trans_len < HDR_BYTES * 8) {
            bad++;
            continue;
        }
        uint32_t magic;
        uint16_t id, row0, rows, width;
        memcpy(&magic, rx, 4);
        memcpy(&id, rx + 4, 2);
        memcpy(&row0, rx + 6, 2);
        memcpy(&rows, rx + 8, 2);
        memcpy(&width, rx + 10, 2);
        if (magic != THM_MAGIC || width != TH_W || rows == 0 || row0 + rows > TH_H ||
            t.trans_len < (size_t)(HDR_BYTES + rows * TH_W) * 8) {
            bad++;
            continue;
        }
        if (id != cur_id) {
            cur_id = id;
            rows_mask = 0;
        }
        memcpy(frames[back] + (size_t)row0 * TH_W, rx + HDR_BYTES, (size_t)rows * TH_W);
        rows_mask |= (uint8_t)(1u << (row0 / CHUNK_ROWS));
        if (rows_mask == 0x0F) { /* all four chunks of this frame */
            thermal_publish_external(frames[back], TH_W, TH_H, rx[13] ? rx[13] : 170);
            back ^= 1;
            rows_mask = 0;
            if (++frames_ok % 300 == 0) ESP_LOGI(TAG, "thermal frames %lu (bad chunks %lu)", (unsigned long)frames_ok,
                                                 (unsigned long)bad);
        }
    }
}

void hub_link_start(void)
{
    const uart_config_t uc = {
        .baud_rate = HUB_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(HUB_UART, 8192, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HUB_UART, &uc));
    ESP_ERROR_CHECK(uart_set_pin(HUB_UART, HUB_UART_TX, HUB_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreatePinnedToCore(uart_task, "hub_uart", 4096, NULL, 5, NULL, 0);

    const gpio_config_t rdy = {.pin_bit_mask = 1ULL << HUB_READY, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rdy);
    gpio_set_level(HUB_READY, 0);
    const spi_bus_config_t bus = {
        .mosi_io_num = HUB_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = HUB_SPI_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };
    const spi_slave_interface_config_t sc = {
        .mode = 0,
        .spics_io_num = HUB_SPI_CS,
        .queue_size = 2,
        .post_setup_cb = ready_high,
        .post_trans_cb = ready_low,
    };
    ESP_ERROR_CHECK(spi_slave_initialize(SPI3_HOST, &bus, &sc, SPI_DMA_CH_AUTO));
    xTaskCreatePinnedToCore(spi_task, "hub_spi", 4096, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "hub link: UART1 %d baud, SPI3 slave for thermal", HUB_BAUD);
}

#else

void hub_link_start(void)
{
}

#endif
