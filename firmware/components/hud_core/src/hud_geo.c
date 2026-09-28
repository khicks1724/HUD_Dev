#include "hud_geo.h"

#include <math.h>

#define WGS84_A 6378137.0
#define WGS84_F (1.0 / 298.257223563)
#define WGS84_E2 (WGS84_F * (2.0 - WGS84_F))
#define DEG2RAD (M_PI / 180.0)
#define RAD2DEG (180.0 / M_PI)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void hud_lla_to_ecef(const hud_lla_t *lla, hud_vec3d_t *ecef)
{
    const double lat = lla->lat_deg * DEG2RAD;
    const double lon = lla->lon_deg * DEG2RAD;
    const double s = sin(lat), c = cos(lat);
    const double n = WGS84_A / sqrt(1.0 - WGS84_E2 * s * s);
    ecef->x = (n + lla->hae_m) * c * cos(lon);
    ecef->y = (n + lla->hae_m) * c * sin(lon);
    ecef->z = (n * (1.0 - WGS84_E2) + lla->hae_m) * s;
}

void hud_lla_to_enu(const hud_lla_t *observer, const hud_lla_t *target, hud_vec3d_t *enu)
{
    hud_vec3d_t o, t;
    hud_lla_to_ecef(observer, &o);
    hud_lla_to_ecef(target, &t);
    const double dx = t.x - o.x, dy = t.y - o.y, dz = t.z - o.z;

    const double lat = observer->lat_deg * DEG2RAD;
    const double lon = observer->lon_deg * DEG2RAD;
    const double sl = sin(lat), cl = cos(lat);
    const double so = sin(lon), co = cos(lon);

    enu->x = -so * dx + co * dy;
    enu->y = -sl * co * dx - sl * so * dy + cl * dz;
    enu->z = cl * co * dx + cl * so * dy + sl * dz;
}

double hud_wrap360(double deg)
{
    deg = fmod(deg, 360.0);
    if (deg < 0) {
        deg += 360.0;
    }
    return deg;
}

double hud_wrap180(double deg)
{
    deg = hud_wrap360(deg);
    if (deg > 180.0) {
        deg -= 360.0;
    }
    return deg;
}

void hud_enu_to_polar(const hud_vec3d_t *enu, hud_polar_t *out)
{
    out->ground_m = sqrt(enu->x * enu->x + enu->y * enu->y);
    out->range_m = sqrt(out->ground_m * out->ground_m + enu->z * enu->z);
    out->bearing_deg = hud_wrap360(atan2(enu->x, enu->y) * RAD2DEG);
    out->elevation_deg = atan2(enu->z, out->ground_m) * RAD2DEG;
}
