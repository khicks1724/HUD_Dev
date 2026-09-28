#include "hud_cot.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- small string helpers (no NUL termination assumed) ---------- */

static const char *mem_find(const char *hay, size_t hay_len, const char *needle)
{
    const size_t n = strlen(needle);
    if (n == 0 || hay_len < n) {
        return NULL;
    }
    for (size_t i = 0; i + n <= hay_len; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, n) == 0) {
            return hay + i;
        }
    }
    return NULL;
}

/* Find "<name" followed by whitespace, '/' or '>'. Returns pointer to '<' and
 * sets *tag_end to the closing '>' of the start tag. */
static const char *find_tag(const char *xml, size_t len, const char *name, const char **tag_end)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "<%s", name);
    const size_t plen = strlen(pat);
    const char *end = xml + len;
    const char *p = xml;
    while (p < end) {
        const char *hit = mem_find(p, (size_t)(end - p), pat);
        if (!hit) {
            return NULL;
        }
        const char *after = hit + plen;
        if (after < end && (isspace((unsigned char)*after) || *after == '/' || *after == '>')) {
            const char *gt = memchr(after, '>', (size_t)(end - after));
            if (!gt) {
                return NULL;
            }
            *tag_end = gt;
            return hit;
        }
        p = after;
    }
    return NULL;
}

static void xml_unescape(char *s)
{
    static const struct {
        const char *ent;
        char ch;
    } map[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    char *w = s;
    for (char *r = s; *r;) {
        if (*r == '&') {
            int matched = 0;
            for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                const size_t n = strlen(map[i].ent);
                if (strncmp(r, map[i].ent, n) == 0) {
                    *w++ = map[i].ch;
                    r += n;
                    matched = 1;
                    break;
                }
            }
            if (matched) {
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

/* Copy attribute value of `name` inside [tag, tag_end) into out. */
static bool get_attr(const char *tag, const char *tag_end, const char *name, char *out, size_t cap)
{
    const size_t n = strlen(name);
    for (const char *p = tag; p + n + 2 < tag_end; p++) {
        if (!isspace((unsigned char)p[0])) {
            continue;
        }
        if (memcmp(p + 1, name, n) != 0) {
            continue;
        }
        const char *q = p + 1 + n;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        if (q >= tag_end || *q != '=') {
            continue;
        }
        q++;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        if (q >= tag_end || (*q != '"' && *q != '\'')) {
            continue;
        }
        const char quote = *q++;
        const char *v_end = memchr(q, quote, (size_t)(tag_end - q));
        if (!v_end) {
            return false;
        }
        size_t vl = (size_t)(v_end - q);
        if (vl >= cap) {
            vl = cap - 1;
        }
        memcpy(out, q, vl);
        out[vl] = '\0';
        xml_unescape(out);
        return true;
    }
    return false;
}

static bool get_attr_double(const char *tag, const char *tag_end, const char *name, double *out)
{
    char tmp[40];
    if (!get_attr(tag, tag_end, name, tmp, sizeof(tmp))) {
        return false;
    }
    char *e = NULL;
    const double v = strtod(tmp, &e);
    if (e == tmp) {
        return false;
    }
    *out = v;
    return true;
}

/* ---------- time ---------- */

/* Howard Hinnant's days_from_civil. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = (int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (int)(yy + (*m <= 2));
}

int64_t hud_cot_parse_time(const char *s, size_t len)
{
    int Y, M, D, h, mi;
    double sec;
    char tmp[40];
    if (len >= sizeof(tmp)) {
        len = sizeof(tmp) - 1;
    }
    memcpy(tmp, s, len);
    tmp[len] = '\0';
    if (sscanf(tmp, "%4d-%2d-%2dT%2d:%2d:%lf", &Y, &M, &D, &h, &mi, &sec) != 6) {
        return 0;
    }
    if (M < 1 || M > 12 || D < 1 || D > 31) {
        return 0;
    }
    const int64_t days = days_from_civil(Y, (unsigned)M, (unsigned)D);
    const int64_t whole = (int64_t)sec;
    const int64_t ms_frac = (int64_t)((sec - (double)whole) * 1000.0 + 0.5);
    int64_t t = ((days * 24 + h) * 60 + mi) * 60 + whole;
    t = t * 1000 + ms_frac;
    /* Honour a trailing +hh:mm / -hh:mm offset if present (rare in CoT). */
    const char *z = strlen(tmp) > 19 ? strpbrk(tmp + 19, "+-") : NULL;
    if (z) {
        int oh = 0, om = 0;
        if (sscanf(z + 1, "%2d:%2d", &oh, &om) >= 1) {
            const int64_t off = ((int64_t)oh * 60 + om) * 60000;
            t += (*z == '+') ? -off : off;
        }
    }
    return t;
}

void hud_cot_format_time(int64_t ms, char out[25])
{
    int64_t secs = ms / 1000;
    int msr = (int)(ms % 1000);
    if (msr < 0) {
        msr += 1000;
        secs -= 1;
    }
    int64_t days = secs / 86400;
    int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    int y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%04d-%02u-%02uT%02d:%02d:%02d.%03dZ", y, m, d, (int)(rem / 3600),
             (int)((rem / 60) % 60), (int)(rem % 60), msr);
    memcpy(out, tmp, 24);
    out[24] = 0;
}

/* ---------- type decoding ---------- */

hud_affil_t hud_cot_affil_from_type(const char *type)
{
    if (!type || type[0] != 'a' || type[1] != '-' || !type[2]) {
        return HUD_AFFIL_UNKNOWN;
    }
    switch (type[2]) {
    case 'f':
    case 'a': /* assumed friend */
        return HUD_AFFIL_FRIEND;
    case 'h':
    case 's': /* suspect */
    case 'j': /* joker */
    case 'k': /* faker */
        return HUD_AFFIL_HOSTILE;
    case 'n':
        return HUD_AFFIL_NEUTRAL;
    default:
        return HUD_AFFIL_UNKNOWN;
    }
}

hud_dim_t hud_cot_dim_from_type(const char *type)
{
    if (!type || type[0] != 'a' || strlen(type) < 5 || type[3] != '-') {
        return HUD_DIM_OTHER;
    }
    switch (type[4]) {
    case 'G':
        return HUD_DIM_GROUND;
    case 'A':
        return HUD_DIM_AIR;
    case 'S':
        return HUD_DIM_SEA;
    case 'U':
        return HUD_DIM_SUBSURFACE;
    case 'P':
        return HUD_DIM_SPACE;
    default:
        return HUD_DIM_OTHER;
    }
}

/* ---------- event parse ---------- */

bool hud_cot_parse_event(const char *xml, size_t len, hud_cot_event_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *ev_end = NULL;
    const char *ev = find_tag(xml, len, "event", &ev_end);
    if (!ev) {
        return false;
    }
    if (!get_attr(ev, ev_end, "uid", out->uid, sizeof(out->uid)) || !out->uid[0]) {
        return false;
    }
    get_attr(ev, ev_end, "type", out->type, sizeof(out->type));

    char tbuf[40];
    if (get_attr(ev, ev_end, "time", tbuf, sizeof(tbuf))) out->time_ms = hud_cot_parse_time(tbuf, strlen(tbuf));
    if (get_attr(ev, ev_end, "start", tbuf, sizeof(tbuf))) out->start_ms = hud_cot_parse_time(tbuf, strlen(tbuf));
    if (get_attr(ev, ev_end, "stale", tbuf, sizeof(tbuf))) out->stale_ms = hud_cot_parse_time(tbuf, strlen(tbuf));

    const size_t rest = len - (size_t)(ev_end - xml);
    const char *pt_end = NULL;
    const char *pt = find_tag(ev_end, rest, "point", &pt_end);
    if (!pt) {
        return false;
    }
    if (!get_attr_double(pt, pt_end, "lat", &out->lat) || !get_attr_double(pt, pt_end, "lon", &out->lon)) {
        return false;
    }
    double v;
    out->hae = get_attr_double(pt, pt_end, "hae", &v) ? v : 0.0;
    out->ce = get_attr_double(pt, pt_end, "ce", &v) ? (float)v : 9999999.0f;
    out->le = get_attr_double(pt, pt_end, "le", &v) ? (float)v : 9999999.0f;
    /* CoT uses 9999999 as "unknown" for hae too. */
    if (out->hae > 9999990.0) {
        out->hae = 0.0;
    }

    const char *t_end = NULL;
    const char *t = find_tag(ev_end, rest, "contact", &t_end);
    if (t) {
        get_attr(t, t_end, "callsign", out->callsign, sizeof(out->callsign));
    }
    t = find_tag(ev_end, rest, "__group", &t_end);
    if (t) {
        get_attr(t, t_end, "name", out->team, sizeof(out->team));
    }
    t = find_tag(ev_end, rest, "track", &t_end);
    if (t) {
        double c = 0, s = 0;
        const bool hc = get_attr_double(t, t_end, "course", &c);
        const bool hs = get_attr_double(t, t_end, "speed", &s);
        out->has_track = hc || hs;
        out->course_deg = (float)c;
        out->speed_mps = (float)s;
    }
    if (!out->callsign[0]) {
        snprintf(out->callsign, sizeof(out->callsign), "%.*s", (int)sizeof(out->callsign) - 1, out->uid);
    }
    out->affil = hud_cot_affil_from_type(out->type);
    out->dim = hud_cot_dim_from_type(out->type);
    return true;
}

/* ---------- stream framing ---------- */

void hud_cot_stream_init(hud_cot_stream_t *s)
{
    memset(s, 0, sizeof(*s));
}

static void stream_consume(hud_cot_stream_t *s, size_t n)
{
    if (n >= s->len) {
        s->len = 0;
        return;
    }
    memmove(s->buf, s->buf + n, s->len - n);
    s->len -= n;
}

void hud_cot_stream_feed(hud_cot_stream_t *s, const char *data, size_t len, hud_cot_event_cb cb, void *ctx)
{
    static const char END_TAG[] = "</event>";
    const size_t end_len = sizeof(END_TAG) - 1;

    while (len > 0) {
        size_t room = sizeof(s->buf) - s->len;
        if (room == 0) {
            /* An event larger than the buffer (e.g. huge <detail>). Drop what
             * we have and resynchronise on the next "<event". */
            s->bytes_dropped += (uint32_t)s->len;
            s->events_bad++;
            s->len = 0;
            room = sizeof(s->buf);
        }
        const size_t n = len < room ? len : room;
        memcpy(s->buf + s->len, data, n);
        s->len += n;
        data += n;
        len -= n;

        for (;;) {
            const char *start = mem_find(s->buf, s->len, "<event");
            if (!start) {
                /* Keep a short tail in case "<event" is split across reads. */
                if (s->len > 6) {
                    s->bytes_dropped += (uint32_t)(s->len - 6);
                    stream_consume(s, s->len - 6);
                }
                break;
            }
            if (start != s->buf) {
                stream_consume(s, (size_t)(start - s->buf));
            }
            const char *end = mem_find(s->buf, s->len, END_TAG);
            if (!end) {
                /* Self-closing <event .../> carries no point; skip it. */
                const char *gt = memchr(s->buf, '>', s->len);
                if (gt && gt > s->buf && gt[-1] == '/') {
                    stream_consume(s, (size_t)(gt - s->buf) + 1);
                    continue;
                }
                break;
            }
            const size_t doc_len = (size_t)(end - s->buf) + end_len;
            hud_cot_event_t ev;
            if (hud_cot_parse_event(s->buf, doc_len, &ev)) {
                s->events_ok++;
                if (cb) {
                    cb(&ev, ctx);
                }
            } else {
                s->events_bad++;
            }
            stream_consume(s, doc_len);
        }
    }
}

/* ---------- building ---------- */

int hud_cot_build_ping(char *out, size_t cap, const char *uid, int64_t now_ms)
{
    char t[25], st[25];
    hud_cot_format_time(now_ms, t);
    hud_cot_format_time(now_ms + 20000, st);
    const int n = snprintf(out, cap,
                           "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                           "<event version=\"2.0\" uid=\"%s-ping\" type=\"t-x-c-t\" how=\"h-g-i-g-o\" "
                           "time=\"%s\" start=\"%s\" stale=\"%s\">"
                           "<point lat=\"0.0\" lon=\"0.0\" hae=\"0.0\" ce=\"9999999\" le=\"9999999\"/>"
                           "<detail/></event>",
                           uid, t, t, st);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}

int hud_cot_build_sa(char *out, size_t cap, const char *uid, const char *callsign, const char *type,
                     double lat, double lon, double hae, float ce, float course_deg, float speed_mps,
                     int64_t now_ms, int stale_s)
{
    char t[25], st[25];
    hud_cot_format_time(now_ms, t);
    hud_cot_format_time(now_ms + (int64_t)stale_s * 1000, st);
    const int n = snprintf(out, cap,
                           "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                           "<event version=\"2.0\" uid=\"%s\" type=\"%s\" how=\"m-g\" "
                           "time=\"%s\" start=\"%s\" stale=\"%s\">"
                           "<point lat=\"%.7f\" lon=\"%.7f\" hae=\"%.1f\" ce=\"%.1f\" le=\"9999999\"/>"
                           "<detail><contact callsign=\"%s\"/>"
                           "<track course=\"%.1f\" speed=\"%.2f\"/>"
                           "<takv device=\"ESP32-S3 prism HUD\" platform=\"HUD_Dev\" os=\"ESP-IDF\" version=\"0.1\"/>"
                           "</detail></event>",
                           uid, type ? type : "a-f-G-U-C", t, t, st, lat, lon, hae, ce, callsign, course_deg,
                           speed_mps);
    return (n < 0 || (size_t)n >= cap) ? -1 : n;
}
