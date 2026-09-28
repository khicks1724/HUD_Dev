#include "hud_render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RADF ((float)M_PI / 180.0f)

#define TAPE_H 22
#define MAX_SORT 64

void hud_format_range(float m, char *out, int cap)
{
    if (m < 1000.0f) {
        snprintf(out, (size_t)cap, "%dm", (int)(m + 0.5f));
    } else if (m < 10000.0f) {
        snprintf(out, (size_t)cap, "%.1fkm", m / 1000.0f);
    } else {
        snprintf(out, (size_t)cap, "%dkm", (int)(m / 1000.0f + 0.5f));
    }
}

static uint16_t affil_color(hud_affil_t a)
{
    switch (a) {
    case HUD_AFFIL_FRIEND: return GFX_CYAN;
    case HUD_AFFIL_HOSTILE: return GFX_RED;
    case HUD_AFFIL_NEUTRAL: return GFX_LIME;
    default: return GFX_YELLOW;
    }
}

/* Unit vector in ENU for a given true bearing / elevation. */
static hud_vec3f_t dir_enu(float bearing_deg, float elev_deg)
{
    const float b = bearing_deg * DEG2RADF, e = elev_deg * DEG2RADF;
    return (hud_vec3f_t){sinf(b) * cosf(e), cosf(b) * cosf(e), sinf(e)};
}

/* Simplified MIL-STD-2525 frame shapes, drawn small for a 240px display. */
static void draw_symbol(gfx_t *g, int x, int y, hud_affil_t a, hud_dim_t d, uint16_t c, bool stale)
{
    const int r = 6;
    switch (a) {
    case HUD_AFFIL_FRIEND:
        if (d == HUD_DIM_AIR) {
            /* friendly air: open-bottom arc */
            for (int i = 0; i <= 180; i += 15) {
                const float t0 = i * DEG2RADF, t1 = (i + 15) * DEG2RADF;
                if (i < 180) {
                    gfx_line(g, x + (int)(r * cosf(t0)), y - (int)(r * sinf(t0)), x + (int)(r * cosf(t1)),
                             y - (int)(r * sinf(t1)), c);
                }
            }
            gfx_hline(g, x - r, y, 2 * r + 1, c);
        } else {
            gfx_rect(g, x - r - 2, y - r + 1, 2 * r + 5, 2 * r - 1, c);
        }
        break;
    case HUD_AFFIL_HOSTILE:
        gfx_diamond(g, x, y, r + 1, c);
        if (d == HUD_DIM_AIR) gfx_hline(g, x - r - 1, y + r + 3, 2 * r + 3, c);
        break;
    case HUD_AFFIL_NEUTRAL:
        gfx_rect(g, x - r, y - r, 2 * r + 1, 2 * r + 1, c);
        break;
    default:
        gfx_circle(g, x, y, r, c);
        gfx_pixel(g, x, y, c);
        break;
    }
    if (!stale) {
        gfx_fill_rect(g, x - 1, y - 1, 3, 3, c);
    }
}

static void draw_heading_tape(gfx_t *g, const hud_scene_t *s, float heading)
{
    const hud_proj_cfg_t *p = &s->proj;
    const float half = atanf((p->width * 0.5f) / p->fx) / DEG2RADF;
    const int start = (int)floorf((heading - half) / 5.0f) * 5;
    for (int d = start; d <= heading + half; d += 5) {
        const float off = (float)d - heading;
        const int x = (int)(p->cx + p->fx * tanf(off * DEG2RADF) + 0.5f);
        const int hd = ((d % 360) + 360) % 360;
        if (hd % 10 == 0) {
            gfx_vline(g, x, 0, 6, GFX_GREEN);
            if (abs(x - (int)p->cx) > 18) {
                char lab[6];
                const char *card = hd == 0 ? "N" : hd == 90 ? "E" : hd == 180 ? "S" : hd == 270 ? "W" : NULL;
                if (card) {
                    snprintf(lab, sizeof(lab), "%s", card);
                } else {
                    snprintf(lab, sizeof(lab), "%d", hd / 10);
                }
                gfx_text_c(g, x, 8, lab, GFX_GREEN, 1);
            }
        } else {
            gfx_vline(g, x, 0, 3, GFX_DIMGRN);
        }
    }
    /* Centre readout box */
    char buf[8];
    snprintf(buf, sizeof(buf), "%03d", ((int)(heading + 0.5f)) % 360);
    const int cx = (int)p->cx;
    gfx_fill_rect(g, cx - 13, 7, 27, 11, GFX_BLACK);
    gfx_rect(g, cx - 13, 6, 27, 13, GFX_GREEN);
    gfx_text_c(g, cx + 1, 9, buf, GFX_WHITE, 1);
    gfx_triangle(g, cx - 3, TAPE_H - 2, cx + 3, TAPE_H - 2, cx, TAPE_H + 1, GFX_GREEN);
}

static void draw_world_line(gfx_t *g, const hud_scene_t *s, float heading, float elev, float half_width_deg,
                            uint16_t c, bool dashed)
{
    /* Draw a short line of constant elevation by projecting its two ends. */
    hud_proj_t a, b;
    hud_project(s->q, dir_enu(heading - half_width_deg, elev), &s->proj, &a);
    hud_project(s->q, dir_enu(heading + half_width_deg, elev), &s->proj, &b);
    if (!a.in_front || !b.in_front) return;
    if (!dashed) {
        gfx_line(g, (int)a.sx, (int)a.sy, (int)b.sx, (int)b.sy, c);
        return;
    }
    const int segs = 6;
    for (int i = 0; i < segs; i += 2) {
        const float t0 = (float)i / segs, t1 = (float)(i + 1) / segs;
        gfx_line(g, (int)(a.sx + (b.sx - a.sx) * t0), (int)(a.sy + (b.sy - a.sy) * t0),
                 (int)(a.sx + (b.sx - a.sx) * t1), (int)(a.sy + (b.sy - a.sy) * t1), c);
    }
}

static void draw_horizon(gfx_t *g, const hud_scene_t *s, float heading)
{
    /* Horizon as two segments with a gap around boresight. */
    hud_proj_t a, b, c, d;
    hud_project(s->q, dir_enu(heading - 60, 0), &s->proj, &a);
    hud_project(s->q, dir_enu(heading - 4, 0), &s->proj, &b);
    hud_project(s->q, dir_enu(heading + 4, 0), &s->proj, &c);
    hud_project(s->q, dir_enu(heading + 60, 0), &s->proj, &d);
    if (a.in_front && b.in_front) gfx_line(g, (int)a.sx, (int)a.sy, (int)b.sx, (int)b.sy, GFX_DIMGRN);
    if (c.in_front && d.in_front) gfx_line(g, (int)c.sx, (int)c.sy, (int)d.sx, (int)d.sy, GFX_DIMGRN);
    for (int el = -20; el <= 20; el += 10) {
        if (el == 0) continue;
        draw_world_line(g, s, heading, (float)el, 3.0f, GFX_DIMGRN, el < 0);
    }
}

static void draw_boresight(gfx_t *g, const hud_scene_t *s)
{
    const int x = (int)(s->proj.cx + 0.5f), y = (int)(s->proj.cy + 0.5f);
    gfx_hline(g, x - 10, y, 7, GFX_GREEN);
    gfx_hline(g, x + 4, y, 7, GFX_GREEN);
    gfx_vline(g, x, y - 10, 7, GFX_GREEN);
    gfx_vline(g, x, y + 4, 7, GFX_GREEN);
}

static void draw_radar(gfx_t *g, const hud_scene_t *s, const hud_rtarget_t *t, int n, float heading)
{
    const int R = 30, cx = g->w - R - 4, cy = g->h - R - 4;
    gfx_circle(g, cx, cy, R, GFX_DIMGRN);
    gfx_pixel(g, cx, cy, GFX_GREEN);
    /* FOV wedge */
    const float half = atanf((s->proj.width * 0.5f) / s->proj.fx);
    gfx_line(g, cx, cy, cx + (int)(R * sinf(-half)), cy - (int)(R * cosf(half)), GFX_DIMGRN);
    gfx_line(g, cx, cy, cx + (int)(R * sinf(half)), cy - (int)(R * cosf(half)), GFX_DIMGRN);
    const float h = heading * DEG2RADF, ch = cosf(h), sh = sinf(h);
    for (int i = 0; i < n; i++) {
        /* rotate ENU so current heading is up */
        const float x = t[i].enu.x * ch - t[i].enu.y * sh;
        const float y = t[i].enu.x * sh + t[i].enu.y * ch;
        const float d = sqrtf(x * x + y * y);
        float k = (float)R / s->radar_range_m;
        if (d * k > R - 2) k = (R - 2) / d; /* clamp to rim */
        const int px = cx + (int)(x * k), py = cy - (int)(y * k);
        gfx_fill_rect(g, px - 1, py - 1, 3, 3, affil_color(t[i].affil));
    }
}

static void draw_status_bar(gfx_t *g, const hud_scene_t *s, int shown, int total)
{
    char buf[32];
    const uint16_t link_c = s->link_state == 2 ? GFX_GREEN : s->link_state == 1 ? GFX_AMBER : GFX_RED;
    const char *link = s->link_state == 2 ? "TAK" : s->link_state == 1 ? "WIFI" : "NOLINK";
    gfx_text(g, 3, g->h - 30, link, link_c, 1);
    snprintf(buf, sizeof(buf), "POS %s", s->own_pos_valid ? s->pos_source : "----");
    gfx_text(g, 3, g->h - 20, buf, s->own_pos_valid ? GFX_GREEN : GFX_RED, 1);
    snprintf(buf, sizeof(buf), "HDG %s", s->hdg_source);
    gfx_text(g, 3, g->h - 10, buf, GFX_GREEN, 1);
    snprintf(buf, sizeof(buf), "%d/%d", shown, total);
    gfx_text(g, g->w - 70 - gfx_text_width(buf, 1), g->h - 10, buf, GFX_GREEN, 1);
}

typedef struct {
    int idx;
    float range;
} sort_item_t;

static int cmp_far_first(const void *a, const void *b)
{
    const float d = ((const sort_item_t *)b)->range - ((const sort_item_t *)a)->range;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static int draw_targets(gfx_t *g, const hud_scene_t *s, const hud_rtarget_t *t, int n, bool labels)
{
    sort_item_t order[MAX_SORT];
    int m = 0;
    for (int i = 0; i < n && m < MAX_SORT; i++) {
        const float r = sqrtf(t[i].enu.x * t[i].enu.x + t[i].enu.y * t[i].enu.y + t[i].enu.z * t[i].enu.z);
        if (s->max_range_m > 0 && r > s->max_range_m) continue;
        order[m].idx = i;
        order[m].range = r;
        m++;
    }
    qsort(order, (size_t)m, sizeof(order[0]), cmp_far_first);

    int shown = 0;
    for (int k = 0; k < m; k++) {
        const hud_rtarget_t *tg = &t[order[k].idx];
        const bool stale = tg->age_s > 30.0f;
        const uint16_t c = stale ? GFX_DIMGRN : affil_color(tg->affil);
        hud_proj_t p;
        hud_project(s->q, tg->enu, &s->proj, &p);
        char rng[12];
        hud_format_range(order[k].range, rng, sizeof(rng));
        const bool label = labels && (m - k) <= s->max_labels; /* nearest N get labels */
        if (p.on_screen) {
            const int x = (int)(p.sx + 0.5f), y = (int)(p.sy + 0.5f);
            if (y < TAPE_H + 4) continue; /* never draw over the heading tape */
            draw_symbol(g, x, y, tg->affil, tg->dim, c, stale);
            if (tg->selected) gfx_rect(g, x - 11, y - 11, 23, 23, GFX_WHITE);
            if (label) {
                gfx_text_c(g, x, y + 10, tg->callsign ? tg->callsign : "?", c, 1);
                gfx_text_c(g, x, y + 19, rng, c, 1);
            }
            shown++;
        } else {
            /* edge cue: small triangle pointing outward + range */
            const float a = p.edge_angle_deg * DEG2RADF;
            const float ux = cosf(a), uy = -sinf(a);
            const int ex = (int)p.edge_x, ey = (int)p.edge_y;
            if (ey < TAPE_H + 4 && uy < 0) continue;
            gfx_triangle(g, ex + (int)(ux * 7), ey + (int)(uy * 7), ex + (int)(-uy * 5), ey + (int)(ux * 5),
                         ex - (int)(-uy * 5), ey - (int)(ux * 5), c);
            if (label) {
                const int lx = ex - (int)(ux * 18), ly = ey - (int)(uy * 14) - 3;
                gfx_text_c(g, lx, ly, rng, c, 1);
            }
        }
    }
    return shown;
}

static void draw_calib(gfx_t *g, const hud_scene_t *s)
{
    const hud_proj_cfg_t *p = &s->proj;
    const int cx = (int)(p->cx + 0.5f), cy = (int)(p->cy + 0.5f);
    gfx_hline(g, 0, cy, g->w, GFX_GREEN);
    gfx_vline(g, cx, 0, g->h, GFX_GREEN);
    for (int deg = -30; deg <= 30; deg += 5) {
        if (deg == 0) continue;
        const int dx = (int)(p->fx * tanf(deg * DEG2RADF));
        const int dy = (int)(p->fy * tanf(deg * DEG2RADF));
        const int len = deg % 10 == 0 ? 8 : 4;
        gfx_vline(g, cx + dx, cy - len / 2, len, GFX_GREEN);
        gfx_hline(g, cx - len / 2, cy - dy, len, GFX_GREEN);
        if (deg % 10 == 0) {
            char b[6];
            snprintf(b, sizeof(b), "%d", deg);
            gfx_text_c(g, cx + dx, cy + 6, b, GFX_DIMGRN, 1);
            gfx_text(g, cx + 6, cy - dy - 3, b, GFX_DIMGRN, 1);
        }
    }
    gfx_circle(g, cx, cy, 20, GFX_WHITE);
    gfx_text_c(g, g->w / 2, 4, "BORESIGHT CAL", GFX_WHITE, 1);
    gfx_text_c(g, g->w / 2, g->h - 12, "AIM + HOLD BTN", GFX_AMBER, 1);
}

static void draw_status(gfx_t *g, const hud_scene_t *s)
{
    gfx_text(g, 4, 4, "HUD STATUS", GFX_WHITE, 2);
    for (int i = 0; i < s->status_count && i < 10; i++) {
        gfx_text(g, 4, 26 + i * 12, s->status_lines[i], GFX_GREEN, 1);
    }
}

/* Nearest-neighbour scale of the thermal crop to the full screen. White-hot is
 * mapped to green so it reads like the rest of the symbology through the
 * prism; black stays black (transparent). */
static void draw_thermal(gfx_t *g, const hud_thermal_t *th, hud_thermal_mode_t mode)
{
    if (!th || !th->px || th->src_w <= 0 || th->src_h <= 0) return;
    const bool hot_only = mode == HUD_THERMAL_HOT;
    for (int y = 0; y < g->h; y++) {
        const int sy = th->src_y + y * th->src_h / g->h;
        if (sy < 0 || sy >= th->h) continue;
        const uint8_t *row = th->px + (size_t)sy * th->w;
        uint16_t *out = g->px + (size_t)y * g->w;
        for (int x = 0; x < g->w; x++) {
            const int sx = th->src_x + x * th->src_w / g->w;
            if (sx < 0 || sx >= th->w) continue;
            int v = row[sx];
            if (hot_only) {
                if (v < th->hot_threshold) continue;
                v = 120 + (v - th->hot_threshold) * 135 / (256 - th->hot_threshold);
            } else {
                v = v * 3 / 4; /* keep symbology brighter than the underlay */
            }
            out[x] = GFX_RGB(v / 3, v, v / 3);
        }
    }
}

void hud_render(gfx_t *g, const hud_scene_t *s, const hud_rtarget_t *t, int n)
{
    gfx_clear(g, GFX_BLACK);
    if (s->thermal_mode != HUD_THERMAL_OFF && s->mode != HUD_MODE_CALIB && s->mode != HUD_MODE_STATUS) {
        draw_thermal(g, s->thermal, s->thermal_mode);
    }
    hud_euler_t e;
    hud_quat_to_euler(s->q, &e);

    switch (s->mode) {
    case HUD_MODE_CALIB:
        draw_calib(g, s);
        return;
    case HUD_MODE_STATUS:
        draw_status(g, s);
        return;
    case HUD_MODE_MINIMAL: {
        draw_boresight(g, s);
        const int shown = draw_targets(g, s, t, n, true);
        (void)shown;
        return;
    }
    case HUD_MODE_NORMAL:
    default:
        break;
    }

    draw_horizon(g, s, e.heading_deg);
    draw_heading_tape(g, s, e.heading_deg);
    draw_boresight(g, s);
    const int shown = draw_targets(g, s, t, n, true);
    draw_radar(g, s, t, n, e.heading_deg);
    draw_status_bar(g, s, shown, n);
    if (!s->own_pos_valid) {
        gfx_text_c(g, g->w / 2, g->h / 2 + 30, "NO OWN POSITION", GFX_RED, 1);
    }
}
