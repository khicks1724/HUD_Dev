/*
 * Render HUD frames on a PC with the exact firmware renderer and write them
 * as PPM images (convert to PNG with tools/ppm_to_png.py).
 *
 *   ./run_tests.sh preview      -> preview_normal.ppm, preview_calib.ppm, ...
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "hud_geo.h"
#include "hud_render.h"

static uint16_t fb[240 * 240];

static void write_ppm(const char *path, const gfx_t *g, int scale)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", g->w * scale, g->h * scale);
    for (int y = 0; y < g->h * scale; y++) {
        for (int x = 0; x < g->w * scale; x++) {
            uint16_t v = g->px[(y / scale) * g->w + x / scale];
            v = (uint16_t)((v >> 8) | (v << 8)); /* stored big-endian */
            const unsigned char rgb[3] = {(unsigned char)((v >> 11) << 3), (unsigned char)(((v >> 5) & 0x3F) << 2),
                                          (unsigned char)((v & 0x1F) << 3)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("wrote %s\n", path);
}

int main(void)
{
    gfx_t g = {fb, 240, 240};
    const hud_lla_t me = {36.5967, -121.8750, 20.0};
    struct {
        const char *cs;
        double e, n, u;
        hud_affil_t a;
        hud_dim_t d;
    } units[] = {
        {"ALPHA1", 120, 320, 2, HUD_AFFIL_FRIEND, HUD_DIM_GROUND},
        {"BRAVO3", -260, 560, -5, HUD_AFFIL_FRIEND, HUD_DIM_GROUND},
        {"UAV12", 300, 1700, 480, HUD_AFFIL_FRIEND, HUD_DIM_AIR},
        {"TGT-H1", 30, 1400, 10, HUD_AFFIL_HOSTILE, HUD_DIM_GROUND},
        {"CIV", -900, 300, 0, HUD_AFFIL_NEUTRAL, HUD_DIM_GROUND},
        {"UNK", 900, -700, 0, HUD_AFFIL_UNKNOWN, HUD_DIM_GROUND},
    };
    const int n = (int)(sizeof(units) / sizeof(units[0]));
    hud_rtarget_t t[8];
    for (int i = 0; i < n; i++) {
        t[i] = (hud_rtarget_t){{(float)units[i].e, (float)units[i].n, (float)units[i].u},
                               units[i].a, units[i].d, units[i].cs, 1.0f, false};
    }
    (void)me;

    hud_scene_t s = {0};
    s.mode = HUD_MODE_NORMAL;
    hud_euler_t e = {8.0f, 2.0f, 4.0f};
    s.q = hud_quat_from_euler(&e);
    hud_proj_cfg_from_fov(&s.proj, 240, 240, 40.0f, 40.0f);
    s.own_pos_valid = true;
    s.pos_source = "TAK";
    s.hdg_source = "GYRO+BORE";
    s.link_state = 2;
    s.max_range_m = 5000;
    s.radar_range_m = 2000;
    s.max_labels = 6;

    hud_render(&g, &s, t, n);
    write_ppm("preview_normal.ppm", &g, 2);

    s.mode = HUD_MODE_CALIB;
    hud_render(&g, &s, t, n);
    write_ppm("preview_calib.ppm", &g, 2);

    /* Synthetic 640x512 thermal frame: cool sky, warm ground, two hot people
     * placed where ALPHA1 and TGT-H1 project, to show the HOT underlay. */
    static uint8_t th[640 * 512];
    const float fx_cam = 320.0f / tanf(25.0f * 3.14159265f / 180.0f);
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 640; x++) th[y * 640 + x] = (uint8_t)(y < 250 ? 40 + y / 12 : 95 + (y - 250) / 8);
    for (int k = 0; k < n; k++) {
        if (units[k].a == HUD_AFFIL_NEUTRAL || units[k].d == HUD_DIM_AIR) continue;
        hud_proj_t p;
        hud_project(s.q, t[k].enu, &s.proj, &p);
        if (!p.on_screen) continue;
        /* HUD pixel -> camera pixel (same boresight, crop scale) */
        const float ax = (p.sx - s.proj.cx) / s.proj.fx, ay = (p.sy - s.proj.cy) / s.proj.fy;
        const int cx = (int)(320 + ax * fx_cam), cy = (int)(256 + ay * fx_cam);
        for (int dy = -14; dy <= 14; dy++)
            for (int dx = -6; dx <= 6; dx++) {
                const int xx = cx + dx, yy = cy + dy;
                if (xx >= 0 && xx < 640 && yy >= 0 && yy < 512) th[yy * 640 + xx] = (uint8_t)(235 - abs(dx) * 4 - abs(dy));
            }
    }
    hud_thermal_t tf = {th, 640, 512, 0, 0, 0, 0, 170, 0, 0, 0};
    const float half = fx_cam * tanf(20.0f * 3.14159265f / 180.0f);
    tf.src_w = tf.src_h = (int)(2 * half);
    tf.src_x = (640 - tf.src_w) / 2;
    tf.src_y = (512 - tf.src_h) / 2;
    s.thermal = &tf;
    s.mode = HUD_MODE_NORMAL;
    s.thermal_mode = HUD_THERMAL_HOT;
    hud_render(&g, &s, t, n);
    write_ppm("preview_thermal_hot.ppm", &g, 2);
    s.thermal_mode = HUD_THERMAL_FULL;
    hud_render(&g, &s, t, n);
    write_ppm("preview_thermal_full.ppm", &g, 2);
    s.thermal_mode = HUD_THERMAL_OFF;

    s.mode = HUD_MODE_MINIMAL;
    hud_render(&g, &s, t, n);
    write_ppm("preview_minimal.ppm", &g, 2);

    /* Crowded company area over USB (RF sim style): long names, one unnamed
     * unit, one stale, one in the crosshair. */
    struct {
        const char *cs;
        double e, n, u;
        hud_affil_t a;
    } crowd[] = {
        {"L_WEPS", -120, 1150, 8, HUD_AFFIL_FRIEND},   {"L_CO", -40, 1180, 10, HUD_AFFIL_FRIEND},
        {"F&R_CO", 90, 1300, 12, HUD_AFFIL_FRIEND},    {"81-1", 160, 1100, 5, HUD_AFFIL_FRIEND},
        {"81-2", 200, 1150, 6, HUD_AFFIL_FRIEND},      {"SCOUTS", 420, 1500, 20, HUD_AFFIL_FRIEND},
        {"Neros_Relay_1", -300, 900, -4, HUD_AFFIL_FRIEND}, {"", -250, 1000, 0, HUD_AFFIL_FRIEND},
        {"OBJ_HAMMER", 60, 2400, 30, HUD_AFFIL_HOSTILE},  {"I_WEPS", -420, 1600, 15, HUD_AFFIL_FRIEND},
    };
    const int nc = (int)(sizeof(crowd) / sizeof(crowd[0]));
    hud_rtarget_t tc[16];
    for (int i = 0; i < nc; i++) {
        tc[i] = (hud_rtarget_t){{(float)crowd[i].e, (float)crowd[i].n, (float)crowd[i].u},
                                crowd[i].a, HUD_DIM_GROUND, crowd[i].cs, i == 7 ? 45.0f : 1.0f, i == 1};
    }
    hud_euler_t e2 = {0.0f, 0.5f, 0.0f};
    s.q = hud_quat_from_euler(&e2);
    s.mode = HUD_MODE_NORMAL;
    s.link_state = 0;
    s.usb_link = true;
    s.pos_source = "USB";
    s.hdg_source = "BORE";
    s.max_labels = 16;
    hud_render(&g, &s, tc, nc);
    write_ppm("preview_crowd_usb.ppm", &g, 2);
    return 0;
}
