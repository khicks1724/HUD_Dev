/*
 * gfx.h - tiny RGB565 framebuffer renderer for the 240x240 HUD.
 *
 * Pixels are stored byte-swapped (big-endian) so the buffer can be pushed to
 * the ST7789 without conversion. Use GFX_RGB() to build colours.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GFX_RGB(r, g, b)                                                                     \
    ((uint16_t)((((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8) |           \
                ((((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) & 0xFF) << 8)))

/* Prism-friendly palette: black is transparent through the beam splitter. */
#define GFX_BLACK   GFX_RGB(0, 0, 0)
#define GFX_WHITE   GFX_RGB(255, 255, 255)
#define GFX_ACCENT  GFX_RGB(255, 255, 255) /* reticle, tape, horizon, text */
#define GFX_ACCENT_DIM GFX_RGB(125, 125, 125)
#define GFX_CYAN    GFX_RGB(80, 200, 255)   /* friendly */
#define GFX_RED     GFX_RGB(255, 60, 60)    /* hostile */
#define GFX_LIME    GFX_RGB(140, 255, 120)  /* neutral */
#define GFX_YELLOW  GFX_RGB(255, 230, 60)   /* unknown */
#define GFX_AMBER   GFX_RGB(255, 170, 0)

typedef struct {
    uint16_t *px;
    int w, h;
} gfx_t;

void gfx_clear(gfx_t *g, uint16_t c);
void gfx_pixel(gfx_t *g, int x, int y, uint16_t c);
void gfx_hline(gfx_t *g, int x, int y, int w, uint16_t c);
void gfx_vline(gfx_t *g, int x, int y, int h, uint16_t c);
void gfx_line(gfx_t *g, int x0, int y0, int x1, int y1, uint16_t c);
void gfx_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_fill_rect(gfx_t *g, int x, int y, int w, int h, uint16_t c);
void gfx_circle(gfx_t *g, int cx, int cy, int r, uint16_t c);
void gfx_fill_circle(gfx_t *g, int cx, int cy, int r, uint16_t c);
void gfx_diamond(gfx_t *g, int cx, int cy, int r, uint16_t c);
void gfx_triangle(gfx_t *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c);

/* 5x7 font, 6px advance at scale 1. Character 0x7F renders as a degree sign. */
void gfx_text(gfx_t *g, int x, int y, const char *s, uint16_t c, int scale);
int gfx_text_width(const char *s, int scale);
/* Centred on x. */
void gfx_text_c(gfx_t *g, int x, int y, const char *s, uint16_t c, int scale);

#ifdef __cplusplus
}
#endif
