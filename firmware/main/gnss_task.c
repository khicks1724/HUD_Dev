/*
 * gnss_task.c - backpack GNSS (u-blox MAX-M10S, NMEA over UART1).
 * Only built into the firmware when CONFIG_HUD_BACKPACK is set.
 */
#include "gnss_task.h"

#include <sys/time.h>

#include "app_state.h"
#include "board.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_nmea.h"
#include "sdkconfig.h"


#define GNSS_UART UART_NUM_1
#define GNSS_BAUD 38400 /* u-blox M10 factory default */

#if CONFIG_HUD_BACKPACK
static const char *TAG = "gnss";

static void gnss_task(void *arg)
{
    (void)arg;
    static hud_nmea_reader_t rd;
    static hud_gnss_t fix;
    hud_nmea_reader_init(&rd);
    char buf[256];
    bool clock_set = false;
    for (;;) {
        const int n = uart_read_bytes(GNSS_UART, buf, sizeof(buf), pdMS_TO_TICKS(200));
        if (n <= 0) continue;
        if (hud_nmea_feed(&rd, buf, (size_t)n, &fix) > 0 && fix.valid) {
            app_set_own(fix.lat_deg, fix.lon_deg, fix.hae_m, POS_GNSS);
            app_lock();
            if (fix.course_valid) {
                g_app.own_course_deg = fix.course_deg;
                g_app.own_speed_mps = fix.speed_mps;
            }
            app_unlock();
            /* GNSS time lets TLS date checks work with no NTP server. */
            if (!clock_set && fix.utc_ms && !app_utc_ms()) {
                const struct timeval tv = {.tv_sec = fix.utc_ms / 1000, .tv_usec = (fix.utc_ms % 1000) * 1000};
                settimeofday(&tv, NULL);
                clock_set = true;
                app_lock();
                g_app.time_valid = true;
                app_unlock();
                ESP_LOGI(TAG, "clock set from GNSS");
            }
        }
    }
}
#endif

void gnss_task_start(void)
{
#if CONFIG_HUD_BACKPACK
    const uart_config_t uc = {
        .baud_rate = GNSS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(GNSS_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GNSS_UART, &uc));
    ESP_ERROR_CHECK(uart_set_pin(GNSS_UART, BACKPACK_GNSS_TX, BACKPACK_GNSS_RX, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    xTaskCreatePinnedToCore(gnss_task, "gnss", 4096, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "GNSS on UART1 @ %d", GNSS_BAUD);
#endif
}
