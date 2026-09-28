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

/* Cycle OFF -> FULL -> HOT -> OFF. */
void thermal_cycle_mode(void);
