/*
 * render_task.c - 30 Hz: snapshot state -> ENU per target -> hud_render ->
 * ST7789. Runs on core 1 next to the IMU task; networking lives on core 0.
 */
#include "render_task.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "imu_task.h"
#include "lcd_st7789.h"
#include "sdkconfig.h"
#include "thermal.h"

static const char *TAG = "render";

#define FRAME_MS 33
#define MAX_DRAW 64

static uint16_t *s_fb;
static hud_rtarget_t s_rt[MAX_DRAW];
static char s_callsigns[MAX_DRAW][HUD_COT_CALLSIGN_LEN];
static char s_status[10][40];
static portMUX_TYPE s_sel_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_sel_valid;
static float s_sel_bearing, s_sel_elev;
static char s_sel_name[HUD_COT_CALLSIGN_LEN];

const uint16_t *render_framebuffer(void)
{
    return s_fb;
}

bool render_selected_target(float *bearing_deg, float *elev_deg, char *name, int cap)
{
    char tmp[HUD_COT_CALLSIGN_LEN];
    taskENTER_CRITICAL(&s_sel_mux);
    const bool ok = s_sel_valid;
    *bearing_deg = s_sel_bearing;
    *elev_deg = s_sel_elev;
    memcpy(tmp, s_sel_name, sizeof(tmp));
    taskEXIT_CRITICAL(&s_sel_mux);
    tmp[sizeof(tmp) - 1] = '\0';
    if (name && cap > 0) snprintf(name, (size_t)cap, "%s", tmp);
    return ok;
}

static int collect_targets(const hud_lla_t *own, int64_t now)
{
    int n = 0;
    hud_targets_t *db = g_app.targets;
    hud_targets_prune(db, now);
    for (int i = 0; i < HUD_MAX_TARGETS && n < MAX_DRAW; i++) {
        const hud_target_t *t = &db->items[i];
        if (!t->used) continue;
        const hud_lla_t tl = {t->ev.lat, t->ev.lon, t->ev.hae};
        hud_vec3d_t enu;
        hud_lla_to_enu(own, &tl, &enu);
        hud_rtarget_t *r = &s_rt[n];
        r->enu = (hud_vec3f_t){(float)enu.x, (float)enu.y, (float)enu.z};
        r->affil = t->ev.affil;
        r->dim = t->ev.dim;
        snprintf(s_callsigns[n], sizeof(s_callsigns[n]), "%s", t->ev.callsign);
        r->callsign = s_callsigns[n];
        r->age_s = (float)(now - t->updated_mono_ms) / 1000.0f;
        r->selected = false;
        n++;
    }
    return n;
}

/* Pick the target closest to the boresight (within 6 deg) for heading
 * alignment: put the real unit in the crosshair and long-press the button. */
static void select_nearest(hud_quat_t q, int n)
{
    float best = 6.0f;
    int idx = -1;
    for (int i = 0; i < n; i++) {
        const hud_vec3f_t b = hud_quat_rotate_inv(q, s_rt[i].enu);
        if (b.y <= 0) continue;
        const float off = atan2f(sqrtf(b.x * b.x + b.z * b.z), b.y) * 57.29578f;
        if (off < best) {
            best = off;
            idx = i;
        }
    }
    hud_polar_t p = {0};
    if (idx >= 0) {
        s_rt[idx].selected = true;
        const hud_vec3d_t e = {s_rt[idx].enu.x, s_rt[idx].enu.y, s_rt[idx].enu.z};
        hud_enu_to_polar(&e, &p);
    }
    taskENTER_CRITICAL(&s_sel_mux);
    s_sel_valid = idx >= 0;
    s_sel_bearing = (float)p.bearing_deg;
    s_sel_elev = (float)p.elevation_deg;
    if (idx >= 0) memcpy(s_sel_name, s_callsigns[idx], sizeof(s_sel_name));
    taskEXIT_CRITICAL(&s_sel_mux);
}

static void render_task(void *arg)
{
    (void)arg;
    gfx_t g = {s_fb, BOARD_LCD_W, BOARD_LCD_H};
    hud_scene_t sc = {0};
    TickType_t wake = xTaskGetTickCount();
    int frames = 0;
    int64_t fps_t0 = app_mono_ms();
    float fps = 0;

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS));
        const int64_t now = app_mono_ms();

        hud_proj_cfg_from_fov(&sc.proj, BOARD_LCD_W, BOARD_LCD_H, g_cfg.hfov_deg, g_cfg.vfov_deg);
        sc.proj.cx += g_cfg.bore_dx_px;
        sc.proj.cy += g_cfg.bore_dy_px;
        sc.q = imu_get_quat();
        sc.max_range_m = g_cfg.max_range_m;
        sc.radar_range_m = g_cfg.radar_range_m;
        sc.max_labels = 16;
        sc.hdg_source = imu_heading_source_name();

        app_lock();
        const hud_lla_t own = g_app.own;
        sc.own_pos_valid = g_app.own_valid && (now - g_app.own_mono_ms) < 30000;
        sc.pos_source = app_pos_src_name(g_app.pos_src);
        sc.link_state = g_app.link_state;
        sc.usb_link = app_usb_link_live(now);
        sc.mode = g_app.mode;
        sc.thermal_mode = g_app.thermal_mode;
        const int n = sc.own_pos_valid ? collect_targets(&own, now) : 0;
        const int total = g_app.targets->count;
        if (sc.mode == HUD_MODE_STATUS) {
            hud_euler_t e;
            hud_quat_to_euler(sc.q, &e);
            snprintf(s_status[0], 40, "IP %s", g_app.ip[0] ? g_app.ip : "-");
            snprintf(s_status[1], 40, "LINK %d  TIME %s", g_app.link_state, g_app.time_valid ? "OK" : "--");
            snprintf(s_status[2], 40, "POS %s %.5f", app_pos_src_name(g_app.pos_src), own.lat_deg);
            snprintf(s_status[3], 40, "    %.5f %.0fm", own.lon_deg, own.hae_m);
            snprintf(s_status[4], 40, "HDG %.1f P %.1f R %.1f", e.heading_deg, e.pitch_deg, e.roll_deg);
            snprintf(s_status[5], 40, "SRC %s IMU %s", imu_heading_source_name(), imu_ok() ? "OK" : "FAIL");
            snprintf(s_status[6], 40, "TRACKS %d  COT %lu", total, (unsigned long)g_app.cot_rx);
            snprintf(s_status[7], 40, "MESH %lu UDP %lu", (unsigned long)g_app.mesh_rx, (unsigned long)g_app.udp_rx);
            snprintf(s_status[8], 40, "FPS %.0f  FOV %.0fx%.0f", fps, g_cfg.hfov_deg, g_cfg.vfov_deg);
            snprintf(s_status[9], 40, "%.39s", g_app.last_error[0] ? g_app.last_error : g_app.hub_status);
            for (int i = 0; i < 10; i++) sc.status_lines[i] = s_status[i];
            sc.status_count = 10;
        }
        app_unlock();

        select_nearest(sc.q, n);
        sc.thermal = thermal_latest();
        hud_render(&g, &sc, s_rt, n);
        lcd_push_frame(s_fb);

        frames++;
        if (now - fps_t0 >= 1000) {
            fps = frames * 1000.0f / (float)(now - fps_t0);
            frames = 0;
            fps_t0 = now;
        }
    }
}

void render_task_start(void)
{
    const lcd_cfg_t lc = {
        .host = BOARD_LCD_HOST,
        .mosi = BOARD_LCD_MOSI,
        .sclk = BOARD_LCD_SCLK,
        .cs = BOARD_LCD_CS,
        .dc = BOARD_LCD_DC,
        .rst = BOARD_LCD_RST,
        .bl = BOARD_LCD_BL,
        .width = BOARD_LCD_W,
        .height = BOARD_LCD_H,
        .pclk_hz = CONFIG_HUD_LCD_PCLK_MHZ * 1000 * 1000,
        .mirror_x = g_cfg.mirror_x,
        .mirror_y = g_cfg.mirror_y,
        .invert_colors = true,
    };
    ESP_ERROR_CHECK(lcd_init(&lc));
    lcd_set_backlight(g_cfg.brightness);
    s_fb = heap_caps_calloc(BOARD_LCD_W * BOARD_LCD_H, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed");
        return;
    }
    xTaskCreatePinnedToCore(render_task, "render", 6144, NULL, 8, NULL, 1);
}
