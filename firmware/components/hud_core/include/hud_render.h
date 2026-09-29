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

/* What to draw in NORMAL mode (bit mask, see hud_layout_mask for presets).
 * 0 means HUD_L_DEFAULT so a zeroed scene draws everything. */
enum {
    HUD_L_TAPE = 1 << 0,       /* heading tape */
    HUD_L_HORIZON = 1 << 1,    /* horizon + pitch ladder */
    HUD_L_RETICLE = 1 << 2,    /* boresight cross */
    HUD_L_RADAR = 1 << 3,      /* mini radar */
    HUD_L_STATUS = 1 << 4,     /* link / pos / hdg corner text */
    HUD_L_NAMES = 1 << 5,      /* unit callsigns */
    HUD_L_RANGES = 1 << 6,     /* unit ranges */
    HUD_L_INFO = 1 << 7,       /* crosshair target readout */
    HUD_L_EDGE = 1 << 8,       /* off-screen arrows */
    HUD_L_ENEMY_ONLY = 1 << 9, /* names/ranges only on hostiles (+ crosshair target) */
    HUD_L_DEFAULT = 0x1FF,
    HUD_L_ALL = 0x3FF,
};

typedef enum {
    HUD_LAYOUT_FULL = 0, /* everything */
    HUD_LAYOUT_CLEAN,    /* tape, reticle, names, crosshair info */
    HUD_LAYOUT_COMBAT,   /* reticle, units, enemy labels, edge arrows */
    HUD_LAYOUT_NAV,      /* tape, horizon, radar, status; units without labels */
    HUD_LAYOUT_COUNT
} hud_layout_t;

/* Layer mask for a named layout. */
uint32_t hud_layout_mask(hud_layout_t layout);

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
    /* Optional 256-entry colour maps (index = intensity) for FULL and HOT;
     * NULL = white-hot greyscale. */
    const uint16_t *lut_full, *lut_hot;
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
    bool usb_link;             /* phone feeding fix/tracks over USB-C */
    float max_range_m;         /* declutter: hide beyond this */
    float radar_range_m;       /* mini radar scale */
    int max_labels;            /* label budget; placement also avoids overlaps */
    uint32_t layers;           /* HUD_L_* mask, 0 = HUD_L_DEFAULT */
    hud_thermal_mode_t thermal_mode;
    const hud_thermal_t *thermal; /* NULL when no camera frame is available */
    /* status page text */
    const char *status_lines[10];
    int status_count;
} hud_scene_t;

void hud_render(gfx_t *g, const hud_scene_t *scene, const hud_rtarget_t *targets, int n_targets);

#define HUD_LABEL_CHARS 10

/* Callsign as shown on the HUD: '_' -> ' ', cut to HUD_LABEL_CHARS ("LONGCALLS."). */
void hud_label_text(const char *callsign, char *out, int cap);

/* "340m", "1.8km", "12km" */
void hud_format_range(float m, char *out, int cap);

#ifdef __cplusplus
}
#endif
