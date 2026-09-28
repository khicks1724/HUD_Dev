#include "hud_projection.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RADF ((float)M_PI / 180.0f)
#define RAD2DEGF (180.0f / (float)M_PI)

void hud_proj_cfg_from_fov(hud_proj_cfg_t *cfg, int width, int height, float hfov_deg, float vfov_deg)
{
    cfg->width = width;
    cfg->height = height;
    cfg->cx = (width - 1) * 0.5f;
    cfg->cy = (height - 1) * 0.5f;
    cfg->fx = (width * 0.5f) / tanf(hfov_deg * 0.5f * DEG2RADF);
    cfg->fy = (height * 0.5f) / tanf(vfov_deg * 0.5f * DEG2RADF);
    cfg->edge_margin_px = 10.0f;
}

void hud_project(hud_quat_t q, hud_vec3f_t enu, const hud_proj_cfg_t *cfg, hud_proj_t *out)
{
    const hud_vec3f_t b = hud_quat_rotate_inv(q, enu);
    const float right = b.x, fwd = b.y, up = b.z;

    out->az_deg = atan2f(right, fwd) * RAD2DEGF;
    out->el_deg = atan2f(up, sqrtf(right * right + fwd * fwd)) * RAD2DEGF;
    out->in_front = fwd > 1e-3f;
    out->on_screen = false;
    out->sx = out->sy = 0;

    if (out->in_front) {
        out->sx = cfg->cx + cfg->fx * (right / fwd);
        out->sy = cfg->cy - cfg->fy * (up / fwd);
        out->on_screen = out->sx >= 0 && out->sx <= cfg->width - 1 &&
                         out->sy >= 0 && out->sy <= cfg->height - 1;
    }

    /* Off-screen cue: ray from screen centre in the direction of the target,
     * clamped to an inset rectangle. Uses the body-frame direction so it also
     * works for targets behind the viewer. */
    float dx = right, dy = up;
    if (!out->in_front && fabsf(dx) < 1e-6f && fabsf(dy) < 1e-6f) {
        dy = -1.0f; /* directly behind: point down */
    }
    if (out->in_front) {
        dx = out->sx - cfg->cx;
        dy = cfg->cy - out->sy;
    }
    out->edge_angle_deg = atan2f(dy, dx) * RAD2DEGF;

    const float hw = cfg->width * 0.5f - cfg->edge_margin_px;
    const float hh = cfg->height * 0.5f - cfg->edge_margin_px;
    const float adx = fabsf(dx) < 1e-9f ? 1e-9f : fabsf(dx);
    const float ady = fabsf(dy) < 1e-9f ? 1e-9f : fabsf(dy);
    const float s = fminf(hw / adx, hh / ady);
    out->edge_x = cfg->cx + dx * s;
    out->edge_y = cfg->cy - dy * s;
}
