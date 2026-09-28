#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialises the ST7789 and starts the 30 Hz HUD render loop (core 1). */
void render_task_start(void);

/* Target currently nearest the boresight (within 6 deg), for heading
 * alignment. Returns false if none. */
bool render_selected_target(float *bearing_deg, float *elev_deg, char *name, int cap);

/* Live framebuffer (240x240 RGB565 big-endian) for the Wi-Fi live view. */
const uint16_t *render_framebuffer(void);
