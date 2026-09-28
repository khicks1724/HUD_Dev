#include "hud_nmea.h"

#include <stdlib.h>
#include <string.h>

#define MAX_FIELDS 20

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool checksum_ok(const char *s)
{
    if (*s != '$') return false;
    uint8_t sum = 0;
    const char *p = s + 1;
    while (*p && *p != '*') sum ^= (uint8_t)*p++;
    if (*p != '*') return false;
    const int hi = hexval(p[1]), lo = hexval(p[2]);
    if (hi < 0 || lo < 0) return false;
    return sum == (uint8_t)(hi * 16 + lo);
}

/* ddmm.mmmm + hemisphere -> signed degrees */
static double parse_coord(const char *v, const char *hemi)
{
    if (!v[0]) return 0.0;
    const double raw = atof(v);
    const int deg = (int)(raw / 100.0);
    double out = deg + (raw - deg * 100.0) / 60.0;
    if (hemi[0] == 'S' || hemi[0] == 'W') out = -out;
    return out;
}

static int split(char *buf, char **f)
{
    int n = 0;
    char *p = buf;
    f[n++] = p;
    while (*p && n < MAX_FIELDS) {
        if (*p == ',') {
            *p = '\0';
            f[n++] = p + 1;
        } else if (*p == '*') {
            *p = '\0';
            break;
        }
        p++;
    }
    return n;
}

static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool hud_nmea_parse_sentence(const char *s, hud_gnss_t *fix)
{
    if (!checksum_ok(s)) {
        fix->sentences_bad++;
        return false;
    }
    char buf[100];
    strncpy(buf, s + 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *f[MAX_FIELDS];
    const int n = split(buf, f);
    if (n < 1 || strlen(f[0]) < 5) {
        fix->sentences_bad++;
        return false;
    }
    const char *id = f[0] + 2; /* skip talker */

    if (strcmp(id, "GGA") == 0 && n >= 12) {
        fix->fix_quality = atoi(f[6]);
        fix->sats = atoi(f[7]);
        fix->hdop = (float)atof(f[8]);
        if (fix->fix_quality > 0 && f[2][0] && f[4][0]) {
            fix->lat_deg = parse_coord(f[2], f[3]);
            fix->lon_deg = parse_coord(f[4], f[5]);
            fix->msl_m = atof(f[9]);
            fix->hae_m = fix->msl_m + atof(f[11]);
            fix->valid = true;
        } else {
            fix->valid = false;
        }
        fix->sentences_ok++;
        return true;
    }
    if (strcmp(id, "RMC") == 0 && n >= 10) {
        const bool active = f[2][0] == 'A';
        if (active) {
            fix->speed_mps = (float)(atof(f[7]) * 0.514444);
            fix->course_valid = f[8][0] != '\0' && fix->speed_mps > 0.5f;
            fix->course_deg = (float)atof(f[8]);
        }
        if (strlen(f[1]) >= 6 && strlen(f[9]) == 6) {
            const int hh = (f[1][0] - '0') * 10 + (f[1][1] - '0');
            const int mm = (f[1][2] - '0') * 10 + (f[1][3] - '0');
            const double ss = atof(f[1] + 4);
            const int dd = (f[9][0] - '0') * 10 + (f[9][1] - '0');
            const int mo = (f[9][2] - '0') * 10 + (f[9][3] - '0');
            const int yy = 2000 + (f[9][4] - '0') * 10 + (f[9][5] - '0');
            const int64_t days = days_from_civil(yy, (unsigned)mo, (unsigned)dd);
            fix->utc_ms = (((days * 24 + hh) * 60 + mm) * 60) * 1000 + (int64_t)(ss * 1000.0 + 0.5);
        }
        fix->sentences_ok++;
        return true;
    }
    return false; /* other sentence types are ignored, not errors */
}

void hud_nmea_reader_init(hud_nmea_reader_t *r)
{
    memset(r, 0, sizeof(*r));
}

int hud_nmea_feed(hud_nmea_reader_t *r, const char *data, size_t len, hud_gnss_t *fix)
{
    int parsed = 0;
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];
        if (c == '$') {
            r->len = 0;
        }
        if (c == '\r' || c == '\n') {
            if (r->len > 0) {
                r->line[r->len] = '\0';
                if (hud_nmea_parse_sentence(r->line, fix)) parsed++;
                r->len = 0;
            }
            continue;
        }
        if (r->len < sizeof(r->line) - 1) {
            r->line[r->len++] = c;
        } else {
            r->len = 0; /* overlong line, resync on next '$' */
        }
    }
    return parsed;
}
