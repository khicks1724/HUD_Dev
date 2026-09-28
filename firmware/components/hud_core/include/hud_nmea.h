/*
 * hud_nmea.h - NMEA-0183 parsing for the optional GNSS receiver
 * (u-blox MAX-M10S on the backpack board). Handles GGA and RMC from any
 * talker (GP, GN, GL, GA, BD).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;         /* fix available */
    double lat_deg, lon_deg;
    double hae_m;       /* MSL altitude + geoid separation from GGA */
    double msl_m;
    int fix_quality;    /* GGA field 6: 0 none, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float */
    int sats;
    float hdop;
    float speed_mps;    /* from RMC */
    float course_deg;   /* from RMC, true */
    bool course_valid;
    int64_t utc_ms;     /* from RMC date+time, 0 until seen */
    uint32_t sentences_ok, sentences_bad;
} hud_gnss_t;

typedef struct {
    char line[100];
    size_t len;
} hud_nmea_reader_t;

void hud_nmea_reader_init(hud_nmea_reader_t *r);

/* Feed UART bytes; updates *fix for every valid sentence. Returns number of
 * sentences parsed in this call. */
int hud_nmea_feed(hud_nmea_reader_t *r, const char *data, size_t len, hud_gnss_t *fix);

/* Parse one sentence (with or without trailing CR/LF). */
bool hud_nmea_parse_sentence(const char *s, hud_gnss_t *fix);

#ifdef __cplusplus
}
#endif
