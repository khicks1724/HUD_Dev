/*
 * hud_cot.h - minimal Cursor-on-Target (CoT) XML handling.
 *
 * This is deliberately not a general XML parser. A TAK Server streaming
 * connection (TCP 8087 / TLS 8089) or the SA multicast mesh (239.2.3.1:6969)
 * delivers a sequence of <event> ... </event> documents; the HUD only needs a
 * handful of attributes from each, so it scans for them directly. That keeps
 * RAM use fixed and avoids pulling an XML library into the firmware.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUD_COT_UID_LEN 64
#define HUD_COT_TYPE_LEN 32
#define HUD_COT_CALLSIGN_LEN 32
#define HUD_COT_TEAM_LEN 16

typedef enum {
    HUD_AFFIL_UNKNOWN = 0,
    HUD_AFFIL_FRIEND,
    HUD_AFFIL_HOSTILE,
    HUD_AFFIL_NEUTRAL,
} hud_affil_t;

typedef enum {
    HUD_DIM_OTHER = 0,
    HUD_DIM_GROUND,
    HUD_DIM_AIR,
    HUD_DIM_SEA,
    HUD_DIM_SUBSURFACE,
    HUD_DIM_SPACE,
} hud_dim_t;

typedef struct {
    char uid[HUD_COT_UID_LEN];
    char type[HUD_COT_TYPE_LEN];
    char callsign[HUD_COT_CALLSIGN_LEN];
    char team[HUD_COT_TEAM_LEN];
    double lat, lon, hae;
    float ce, le;
    float course_deg, speed_mps;
    bool has_track;
    int64_t time_ms, start_ms, stale_ms; /* ms since Unix epoch, 0 if missing */
    hud_affil_t affil;
    hud_dim_t dim;
} hud_cot_event_t;

/* Parse one complete <event>...</event> document. Returns false if the
 * document is not an event or lacks a uid/point. */
bool hud_cot_parse_event(const char *xml, size_t len, hud_cot_event_t *out);

/* Parse "2026-09-28T14:44:09.123Z" -> ms since epoch. Returns 0 on failure. */
int64_t hud_cot_parse_time(const char *s, size_t len);

/* Format ms since epoch as CoT time "YYYY-MM-DDTHH:MM:SS.mmmZ" (25 bytes incl NUL). */
void hud_cot_format_time(int64_t ms, char out[25]);

hud_affil_t hud_cot_affil_from_type(const char *type);
hud_dim_t hud_cot_dim_from_type(const char *type);

/* ---------- stream framing ---------- */

#ifndef HUD_COT_STREAM_BUF
#define HUD_COT_STREAM_BUF 8192
#endif

typedef void (*hud_cot_event_cb)(const hud_cot_event_t *ev, void *ctx);

typedef struct {
    char buf[HUD_COT_STREAM_BUF];
    size_t len;
    uint32_t events_ok;
    uint32_t events_bad;
    uint32_t bytes_dropped;
} hud_cot_stream_t;

void hud_cot_stream_init(hud_cot_stream_t *s);
/* Feed raw bytes from the socket; calls cb for every complete event. */
void hud_cot_stream_feed(hud_cot_stream_t *s, const char *data, size_t len, hud_cot_event_cb cb, void *ctx);

/* ---------- building ---------- */

/* TAK "ping" (t-x-c-t) keep-alive. Returns bytes written (excl NUL) or -1. */
int hud_cot_build_ping(char *out, size_t cap, const char *uid, int64_t now_ms);

/* Own situational-awareness report (a-f-G-U-C by default). */
int hud_cot_build_sa(char *out, size_t cap, const char *uid, const char *callsign, const char *type,
                     double lat, double lon, double hae, float ce, float course_deg, float speed_mps,
                     int64_t now_ms, int stale_s);

#ifdef __cplusplus
}
#endif
