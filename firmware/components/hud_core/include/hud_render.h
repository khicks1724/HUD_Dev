/*
 * hud_render.h - draws the HUD symbology into a gfx framebuffer.
 *
 * Pure C (no ESP-IDF) so the exact same frame can be rendered on a PC:
 * firmware/test_host/render_preview.c writes it to a PPM image.
 */
#pragma once

#include <stdbool.h>

#include "gfx.h"
#include "hud_attitude.h"
#include "hud_cot.h"
#include "hud_projection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HUD_MODE_NORMAL = 0, /* heading tape, horizon, targets, mini radar */
    HUD_MODE_MINIMAL,    /* targets only, for bright backgrounds / low clutter */
    HUD_MODE_CALIB,      /* boresight crosshair + angular grid for FOV calibration */
    HUD_MODE_STATUS,     /* text status page */
    HUD_MODE_COUNT
} hud_mode_t;

typedef enum {
    HUD_THERMAL_OFF = 0,
    HUD_THERMAL_FULL,     /* whole thermal image under the symbology */
    HUD_THERMAL_HOT,      /* only pixels above a threshold: keeps the prism see-through */
    HUD_THERMAL_COUNT
} hud_thermal_mode_t;

/* 8-bit greyscale thermal frame (white-hot, after the camera's AGC). */
typedef struct {
    const uint8_t *px;
    int w, h;
    /* Portion of the HUD field of view the camera image covers, in HUD pixels.
     * With a 50 deg lens and a ~40 deg prism the camera is wider than the
     * display, so the image is cropped: dst covers the whole screen and src
     * is the central crop. See docs/THERMAL.md. */
    int src_x, src_y, src_w, src_h;
    uint8_t hot_threshold;
    /* Camera-to-display alignment, applied when drawing (docs/THERMAL.md):
     * a side-mounted camera needs a fixed shift and a small roll correction.
     * shift is in HUD pixels, roll in degrees (+ = image rotates clockwise). */
    float shift_x, shift_y, roll_deg;
} hud_thermal_t;

typedef struct {
    hud_vec3f_t enu;     /* metres, observer -> target */
    hud_affil_t affil;
    hud_dim_t dim;
    const char *callsign;
    float age_s;         /* since last update */
    bool selected;
} hud_rtarget_t;

typedef struct {
    hud_mode_t mode;
    hud_quat_t q;              /* world_from_body */
    hud_proj_cfg_t proj;
    bool own_pos_valid;
    const char *pos_source;    /* "FAKE", "TAK", "GNSS", "UDP", "MAN" */
    const char *hdg_source;    /* "GYRO", "MAG", "PHONE", "BORE" */
    int link_state;            /* 0 none, 1 wifi, 2 wifi + TAK stream */
    float max_range_m;         /* declutter: hide beyond this */
    float radar_range_m;       /* mini radar scale */
    int max_labels;
    hud_thermal_mode_t thermal_mode;
    const hud_thermal_t *thermal; /* NULL when no camera frame is available */
    /* status page text */
    const char *status_lines[10];
    int status_count;
} hud_scene_t;

void hud_render(gfx_t *g, const hud_scene_t *scene, const hud_rtarget_t *targets, int n_targets);

/* "340m", "1.8km", "12km" */
void hud_format_range(float m, char *out, int cap);

#ifdef __cplusplus
}
#endif
