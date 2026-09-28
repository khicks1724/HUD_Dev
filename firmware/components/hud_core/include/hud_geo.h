/*
 * hud_geo.h - WGS-84 geodesy for the TAK HUD.
 *
 * Pure C, no ESP-IDF dependencies: builds on the ESP32-S3 and on a desktop
 * compiler (see firmware/test_host).
 *
 * Frames
 *   LLA  : latitude/longitude in degrees, height above ellipsoid (HAE) metres.
 *          CoT <point hae=".."> is already HAE, so no geoid model is needed
 *          as long as the observer altitude is HAE too (NMEA GGA gives MSL +
 *          geoid separation; hud_nmea converts).
 *   ECEF : Earth-centred Earth-fixed, metres.
 *   ENU  : local tangent plane at the observer. x = East, y = North, z = Up.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double lat_deg;
    double lon_deg;
    double hae_m;
} hud_lla_t;

typedef struct {
    double x, y, z;
} hud_vec3d_t;

/* Geodetic -> ECEF (WGS-84). */
void hud_lla_to_ecef(const hud_lla_t *lla, hud_vec3d_t *ecef);

/* Vector from observer to target expressed in the observer's ENU frame. */
void hud_lla_to_enu(const hud_lla_t *observer, const hud_lla_t *target, hud_vec3d_t *enu);

/* Convenience values derived from an ENU vector. */
typedef struct {
    double range_m;       /* slant range */
    double ground_m;      /* horizontal distance */
    double bearing_deg;   /* true bearing, 0..360, clockwise from north */
    double elevation_deg; /* +up */
} hud_polar_t;

void hud_enu_to_polar(const hud_vec3d_t *enu, hud_polar_t *out);

/* Wrap an angle to [0, 360) and (-180, 180]. */
double hud_wrap360(double deg);
double hud_wrap180(double deg);

#ifdef __cplusplus
}
#endif
