/*
 * telemetry.h - live view of the HUD for the web page (docs/LIVE_VIEW.md).
 *
 * Over USB-C: "stream on" on the console prints one line per update
 *     @HUD {"att":{...},"own":{...},"targets":[...]}
 * which the web page reads with the Web Serial API and re-renders with the
 * same projection math. (115200 baud is too slow for raw frames.)
 *
 * Over Wi-Fi: an HTTP server on port 80 (CORS open, so a page opened from
 * file:// can call it):
 *     GET /api/state        same JSON as the serial line
 *     GET /api/frame        exact framebuffer, 240x240 RGB565 big-endian
 *     GET /api/mode?m=0..3  set display mode
 *     GET /api/thermal      cycle thermal underlay
 *     GET /api/hdg?deg=N    set true heading (boresight alignment)
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Build the state JSON; returns length or -1 if it did not fit. */
int telemetry_state_json(char *buf, size_t cap, int max_targets);

void telemetry_serial_enable(bool on, int hz);

/* Start the Wi-Fi HTTP API (call after Wi-Fi init; it waits for an IP). */
void telemetry_http_start(void);
