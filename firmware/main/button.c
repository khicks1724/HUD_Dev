/*
 * button.c - one-button UI on the BOOT key (GPIO0).
 *
 *   short press          next display mode (NORMAL -> MINIMAL -> CALIB -> STATUS)
 *   double press         cycle thermal underlay (OFF -> FULL -> HOT)
 *   long press (>1 s)    NORMAL/MINIMAL: align heading to the target in the
 *                        crosshair (you are looking at the real unit)
 *                        CALIB: level trim (you are looking at the true horizon)
 */
#include "button.h"

#include "app_state.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "imu_task.h"
#include "render_task.h"
#include "thermal.h"

static const char *TAG = "btn";

#define POLL_MS 20
#define LONG_MS 1000
#define DOUBLE_MS 350

static void on_short(void)
{
    app_lock();
    g_app.mode = (hud_mode_t)((g_app.mode + 1) % HUD_MODE_COUNT);
    g_cfg.mode = g_app.mode;
    app_unlock();
}

static void on_long(void)
{
    app_lock();
    const hud_mode_t mode = g_app.mode;
    app_unlock();
    if (mode == HUD_MODE_CALIB) {
        imu_trim_level();
        hud_config_save();
        return;
    }
    float bearing, elev;
    char name[32];
    if (render_selected_target(&bearing, &elev, name, sizeof(name))) {
        imu_set_heading(bearing, 1.0f, HDG_BORE);
        ESP_LOGI(TAG, "heading aligned to %s at %.1f deg", name, bearing);
    } else {
        ESP_LOGW(TAG, "no target within 6 deg of the crosshair to align to");
    }
}

static void button_task(void *arg)
{
    (void)arg;
    int held = 0;
    int since_release = 10000;
    bool pending_short = false;
    bool was_down = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        const bool down = gpio_get_level(BOARD_BTN_BOOT) == 0;
        if (down) {
            held += POLL_MS;
            if (held == LONG_MS) {
                pending_short = false;
                on_long();
            }
        } else if (was_down) {
            if (held < LONG_MS) {
                if (pending_short && since_release < DOUBLE_MS) {
                    pending_short = false;
                    thermal_cycle_mode();
                } else {
                    pending_short = true;
                }
            }
            held = 0;
            since_release = 0;
        } else {
            since_release += POLL_MS;
            if (pending_short && since_release >= DOUBLE_MS) {
                pending_short = false;
                on_short();
            }
        }
        was_down = down;
    }
}

void button_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BTN_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreatePinnedToCore(button_task, "btn", 3072, NULL, 3, NULL, 0);
}
