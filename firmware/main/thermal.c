#include "thermal.h"

#include <math.h>
#include <string.h>

#include "app_state.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "sdkconfig.h"

static const char *TAG = "thermal";

/* Boson 640 geometry. With the 50 deg HFOV lens (21640AS50) the camera is
 * wider than the ~40 deg prism view, so only the central part is shown. */
#define TH_W 640
#define TH_H 512
#define CAM_HFOV_DEG 50.0f

static uint8_t *s_frame[2];
static int s_front;
static hud_thermal_t s_desc;
static bool s_available;

#if CONFIG_HUD_THERMAL_SYNTHETIC || CONFIG_HUD_THERMAL_DVP
/* Compute the camera-pixel crop that maps onto the HUD field of view,
 * assuming the camera is boresighted with the HUD (docs/THERMAL.md). */
static void compute_crop(float hud_hfov_deg, float hud_vfov_deg)
{
    const float fx_cam = (TH_W * 0.5f) / tanf(CAM_HFOV_DEG * 0.5f * (float)M_PI / 180.0f);
    const float half_w = fx_cam * tanf(hud_hfov_deg * 0.5f * (float)M_PI / 180.0f);
    const float half_h = fx_cam * tanf(hud_vfov_deg * 0.5f * (float)M_PI / 180.0f); /* square pixels */
    s_desc.src_w = (int)(2 * half_w);
    s_desc.src_h = (int)(2 * half_h);
    if (s_desc.src_w > TH_W) s_desc.src_w = TH_W;
    if (s_desc.src_h > TH_H) s_desc.src_h = TH_H;
    s_desc.src_x = (TH_W - s_desc.src_w) / 2;
    s_desc.src_y = (TH_H - s_desc.src_h) / 2;
}
#endif

#if CONFIG_HUD_THERMAL_SYNTHETIC
/* Test pattern: cool sky, warmer ground, two moving hot spots. */
static void synth_task(void *arg)
{
    (void)arg;
    for (;;) {
        const int back = s_front ^ 1;
        uint8_t *f = s_frame[back];
        const float t = app_mono_ms() / 1000.0f;
        const int bx1 = (int)(TH_W / 2 + 180 * sinf(t * 0.3f)), by1 = TH_H / 2 + 40;
        const int bx2 = (int)(TH_W / 2 - 120 + 60 * cosf(t * 0.5f)), by2 = TH_H / 2 + 90;
        for (int y = 0; y < TH_H; y++) {
            const int base = y < TH_H / 2 ? 30 + y / 16 : 90 + (y - TH_H / 2) / 6;
            for (int x = 0; x < TH_W; x++) {
                int v = base;
                const int d1 = (x - bx1) * (x - bx1) + (y - by1) * (y - by1) * 4;
                const int d2 = (x - bx2) * (x - bx2) + (y - by2) * (y - by2) * 4;
                if (d1 < 900) v = 240 - d1 / 20;
                if (d2 < 600) v = 225 - d2 / 20;
                f[y * TH_W + x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
        s_front = back;
        vTaskDelay(pdMS_TO_TICKS(66)); /* ~15 fps is plenty for a test pattern */
    }
}
#endif

void thermal_start(void)
{
#if CONFIG_HUD_THERMAL_SYNTHETIC || CONFIG_HUD_THERMAL_DVP
    for (int i = 0; i < 2; i++) {
        s_frame[i] = heap_caps_calloc(TH_W * TH_H, 1, MALLOC_CAP_SPIRAM);
        if (!s_frame[i]) {
            ESP_LOGE(TAG, "no PSRAM for thermal frames");
            return;
        }
    }
    s_desc.w = TH_W;
    s_desc.h = TH_H;
    s_desc.hot_threshold = 170;
    compute_crop(g_cfg.hfov_deg, g_cfg.vfov_deg);
#endif
#if CONFIG_HUD_THERMAL_SYNTHETIC
    s_available = true;
    xTaskCreatePinnedToCore(synth_task, "thermal", 3072, NULL, 2, NULL, 0);
    ESP_LOGI(TAG, "synthetic thermal source running (crop %dx%d)", s_desc.src_w, s_desc.src_h);
#elif CONFIG_HUD_THERMAL_DVP
    ESP_LOGW(TAG, "Boson DVP capture not implemented yet - see docs/THERMAL.md");
#endif
}

static hud_thermal_t s_ext;
static volatile bool s_ext_valid;

void thermal_publish_external(const uint8_t *px, int w, int h, uint8_t hot_threshold)
{
    /* The hub already cropped to the HUD FOV: show the whole frame. */
    s_ext = (hud_thermal_t){px, w, h, 0, 0, w, h, hot_threshold, 0, 0, 0};
    s_ext_valid = true;
    s_available = true;
}

const hud_thermal_t *thermal_latest(void)
{
    hud_thermal_t *t = NULL;
    if (s_ext_valid) {
        t = &s_ext;
    } else if (s_available && s_frame[s_front]) {
        s_desc.px = s_frame[s_front];
        t = &s_desc;
    }
    if (t) { /* side-mounted camera alignment from the console "thal" command */
        t->shift_x = g_cfg.th_shift_x;
        t->shift_y = g_cfg.th_shift_y;
        t->roll_deg = g_cfg.th_roll_deg;
    }
    return t;
}

bool thermal_available(void)
{
    return s_available;
}

void thermal_cycle_mode(void)
{
    app_lock();
    g_app.thermal_mode = (hud_thermal_mode_t)((g_app.thermal_mode + 1) % HUD_THERMAL_COUNT);
    if (!s_available) g_app.thermal_mode = HUD_THERMAL_OFF;
    app_unlock();
}
