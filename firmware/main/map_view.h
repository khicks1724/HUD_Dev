#pragma once

/*
 * map_view.h - the TAK map picture from a host plugin (WinTAK / ATAK).
 *
 * The host captures its map (imagery, unit icons, overlays), JPEG-encodes
 * it and sends it as a usb_link packet (type 4). We decode it with the
 * ESP32-S3's ROM JPEG decoder into an RGB565 picture that the renderer
 * shows full screen in MAP mode, or small in place of the radar.
 */
#include <stdbool.h>
#include <stdint.h>

void map_view_init(void);

/* Decode one baseline JPEG (any size up to 320x320); false if it isn't one. */
bool map_view_publish_jpeg(const uint8_t *jpg, uint32_t len);

/* Latest picture (GFX byte order) or NULL if none in the last 5 s. */
const uint16_t *map_view_latest(int *w, int *h);

float map_view_fps(void);
