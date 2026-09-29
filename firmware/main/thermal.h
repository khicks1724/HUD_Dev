/*
 * thermal.h - optional FLIR Boson thermal underlay (docs/THERMAL.md).
 *
 * Status: interface + synthetic test pattern only. The capture back-end for
 * the Boson's 8-bit CMOS port (via 1.8 V -> 3.3 V translators into the
 * ESP32-S3 LCD_CAM/DVP peripheral) is described in docs/THERMAL.md and is a
 * next step once the flex adapter exists. The renderer, the toggle and the
 * FOV crop are real and can be exercised now with the synthetic source.
 */
#pragma once

#include <stdbool.h>

#include "hud_render.h"

void thermal_start(void);

/* Latest frame or NULL. The returned frame stays valid until the next call. */
const hud_thermal_t *thermal_latest(void);

bool thermal_available(void);

/* A full, already-cropped frame from another source (the hub backpack).
 * The buffer must stay valid until the next call. */
void thermal_publish_external(const uint8_t *px, int w, int h, uint8_t hot_threshold);

/* A frame from the USB host (WinTAK plugin + RPX camera): w x h intensity,
 * camera horizontal FOV in degrees, boresighted with the HUD. The HUD works
 * out where it lands (a 32 deg camera fills the middle of a 40 deg prism).
 * px NULL = source off. The buffer must stay valid until the next call. */
void thermal_publish_usb(const uint8_t *px, int w, int h, float hfov_deg);

/* 256 x RGB888 colour map, index = intensity. Applies to every source. */
void thermal_set_palette(const uint8_t rgb[768]);

/* "usb", "hub", "synthetic" or "none", and USB frames per second. */
const char *thermal_source_name(void);
float thermal_usb_fps(void);

/* Cycle OFF -> FULL -> HOT -> OFF. */
void thermal_cycle_mode(void);
