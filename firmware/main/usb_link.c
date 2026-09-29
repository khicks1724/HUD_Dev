/*
 * usb_link.c - reads the USB-C serial port: console text lines plus binary
 * thermal packets from a host (usb_link.h). Replaces the esp_console REPL so
 * a 20 KB thermal frame and a burst of track lines can share one 2 Mbit/s
 * link without the line editor in the way.
 */
#include "usb_link.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "thermal.h"

static const char *TAG = "usb";

#define LINK_UART UART_NUM_0
#define LINK_BAUD 2000000 /* boot log stays at 115200, then we switch */
#define RX_RING (32 * 1024)
#define LINK_LINE_MAX 256
#define HDR_LEN 12
#define MAX_FRAME (320 * 256)
#define N_FRAMES 3 /* one being received, one shown, one spare */

enum { PKT_THERMAL = 1, PKT_PALETTE = 2, PKT_THERMAL_OFF = 3 };

typedef enum { S_TEXT, S_MAGIC2, S_HDR, S_BODY, S_SUM } rx_state_t;

static uint8_t *s_frames[N_FRAMES];
static uint32_t s_ok, s_bad, s_resync;
static int s_write;
static uint8_t s_pal[768];

static void run_line(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    if (!*line) return;
    int ret = 0;
    const esp_err_t err = esp_console_run(line, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("unknown command: %s\n", line);
    } else if (err == ESP_OK && ret != 0) {
        printf("error %d\n", ret);
    }
    fflush(stdout);
}

static void deliver(uint8_t type, uint16_t w, uint16_t h, uint16_t hfov_cdeg, const uint8_t *body, uint32_t len)
{
    switch (type) {
    case PKT_THERMAL:
        if (w == 0 || h == 0 || (uint32_t)w * h != len) return;
        thermal_publish_usb(body, w, h, hfov_cdeg / 100.0f);
        s_write = (s_write + 1) % N_FRAMES; /* next frame goes to another buffer */
        break;
    case PKT_PALETTE:
        if (len == sizeof(s_pal)) thermal_set_palette(body);
        break;
    case PKT_THERMAL_OFF:
        thermal_publish_usb(NULL, 0, 0, 0);
        break;
    default:
        break;
    }
}

static void link_task(void *arg)
{
    (void)arg;
    static uint8_t chunk[1024];
    char line[LINK_LINE_MAX];
    int line_len = 0;
    bool line_overflow = false;
    rx_state_t st = S_TEXT;
    uint8_t hdr[HDR_LEN];
    int got = 0;
    uint8_t type = 0;
    uint16_t w = 0, h = 0, fov = 0, sum = 0;
    uint32_t len = 0;
    uint8_t *body = NULL;
    uint8_t sumb[2];

    for (;;) {
        const int n = uart_read_bytes(LINK_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
        if (n <= 0 && st != S_TEXT) {
            /* A packet stalled for 20 ms: bytes were lost. Drop it so the
             * next text line or A5 5A is read normally instead of being
             * swallowed as packet body. */
            st = S_TEXT;
            line_len = 0;
            s_resync++;
            continue;
        }
        for (int i = 0; i < n; i++) {
            const uint8_t c = chunk[i];
            switch (st) {
            case S_TEXT:
                if (c == 0xA5 && line_len == 0) {
                    st = S_MAGIC2;
                } else if (c == '\n' || c == '\r') {
                    line[line_len] = '\0';
                    if (!line_overflow) run_line(line);
                    line_len = 0;
                    line_overflow = false;
                } else if (line_len < LINK_LINE_MAX - 1) {
                    line[line_len++] = (char)c;
                } else {
                    line_overflow = true; /* too long: drop the whole line */
                }
                break;
            case S_MAGIC2:
                if (c == 0x5A) {
                    st = S_HDR;
                    got = 0;
                } else {
                    st = S_TEXT;
                }
                break;
            case S_HDR:
                hdr[got++] = c;
                if (got == HDR_LEN) {
                    type = hdr[0];
                    w = (uint16_t)(hdr[2] | hdr[3] << 8);
                    h = (uint16_t)(hdr[4] | hdr[5] << 8);
                    fov = (uint16_t)(hdr[6] | hdr[7] << 8);
                    len = (uint32_t)hdr[8] | (uint32_t)hdr[9] << 8 | (uint32_t)hdr[10] << 16 | (uint32_t)hdr[11] << 24;
                    body = type == PKT_THERMAL ? s_frames[s_write] : s_pal;
                    const uint32_t cap = type == PKT_THERMAL ? MAX_FRAME : sizeof(s_pal);
                    if (!body || len > cap) {
                        ESP_LOGW(TAG, "bad packet type %u len %lu", type, (unsigned long)len);
                        st = S_TEXT; /* resync on the next A5 5A */
                        break;
                    }
                    sum = 0;
                    for (int k = 0; k < HDR_LEN; k++) sum += hdr[k];
                    got = 0;
                    st = len ? S_BODY : S_SUM;
                }
                break;
            case S_BODY: {
                /* copy as much of this chunk as belongs to the body */
                uint32_t take = len - (uint32_t)got;
                if (take > (uint32_t)(n - i)) take = (uint32_t)(n - i);
                memcpy(body + got, chunk + i, take);
                for (uint32_t k = 0; k < take; k++) sum += chunk[i + k];
                got += (int)take;
                i += (int)take - 1;
                if ((uint32_t)got == len) {
                    st = S_SUM;
                    got = 0;
                }
                break;
            }
            case S_SUM:
                sumb[got++] = c;
                if (got == 2) {
                    if ((uint16_t)(sumb[0] | sumb[1] << 8) == sum) {
                        deliver(type, w, h, fov, body, len);
                        s_ok++;
                    } else {
                        s_bad++;
                    }
                    st = S_TEXT;
                    line_len = 0;
                }
                break;
            }
        }
    }
}

void usb_link_stats(uint32_t *ok, uint32_t *bad, uint32_t *resync)
{
    *ok = s_ok;
    *bad = s_bad;
    *resync = s_resync;
}

void usb_link_start(void)
{
    for (int i = 0; i < N_FRAMES; i++) {
        s_frames[i] = heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM);
        if (!s_frames[i]) ESP_LOGE(TAG, "no PSRAM for thermal frames");
    }
    fflush(stdout);
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, RX_RING, 4096, 0, NULL, 0));
    uart_vfs_dev_use_driver(LINK_UART);
    ESP_LOGI(TAG, "switching the USB link to %d baud", LINK_BAUD);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(20));
    uart_wait_tx_done(LINK_UART, pdMS_TO_TICKS(100));
    uart_set_baudrate(LINK_UART, LINK_BAUD);
    setvbuf(stdin, NULL, _IONBF, 0);
    xTaskCreatePinnedToCore(link_task, "usb_link", 4096 + LINK_LINE_MAX, NULL, 6, NULL, 0);
    ESP_LOGI(TAG, "USB link at %d baud: console lines + thermal packets", LINK_BAUD);
}
