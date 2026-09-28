/*
 * hud_projection.h - ENU target vector -> HUD pixel.
 *
 * Pipeline (see docs/HUD_MATH.md):
 *   target ENU  --q_world_from_body^-1-->  HUD body frame (right, fwd, up)
 *               --pinhole-->               screen (sx, sy)
 *
 * The prism mirror is NOT handled here: the renderer draws a normal,
 * un-mirrored image and the ST7789 MADCTL mirror bits flip it for the prism.
 */
#pragma once

#include <stdbool.h>

#include "hud_attitude.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int width;      /* pixels */
    int height;     /* pixels */
    float cx, cy;   /* optical centre (pixels); boresight calibration moves this */
    float fx, fy;   /* focal length in pixels (= px per unit tan(angle)) */
    float edge_margin_px;
} hud_proj_cfg_t;

/* Fill a config from display size and horizontal/vertical FOV in degrees. */
void hud_proj_cfg_from_fov(hud_proj_cfg_t *cfg, int width, int height, float hfov_deg, float vfov_deg);

typedef struct {
    bool in_front;     /* target is in the forward hemisphere */
    bool on_screen;    /* projected point lies inside the display */
    float sx, sy;      /* projected pixel (valid if in_front) */
    float edge_x, edge_y; /* clamped border position for off-screen cue */
    float edge_angle_deg; /* direction of the cue, 0 = right, 90 = up */
    float az_deg;      /* horizontal angle off boresight, + right */
    float el_deg;      /* vertical angle off boresight, + up */
} hud_proj_t;

void hud_project(hud_quat_t q_world_from_body, hud_vec3f_t enu, const hud_proj_cfg_t *cfg, hud_proj_t *out);

#ifdef __cplusplus
}
#endif
