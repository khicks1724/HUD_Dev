#include "hud_takproto.h"

#include <string.h>

typedef struct {
    const uint8_t *p, *end;
} pb_t;

static bool pb_varint(pb_t *b, uint64_t *out)
{
    uint64_t v = 0;
    for (int shift = 0; shift < 64 && b->p < b->end; shift += 7) {
        const uint8_t c = *b->p++;
        v |= (uint64_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

static bool pb_double(pb_t *b, double *out)
{
    if (b->end - b->p < 8) return false;
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) u = (u << 8) | b->p[i]; /* little endian */
    b->p += 8;
    memcpy(out, &u, sizeof(*out));
    return true;
}

/* Read a key; returns false at end. */
static bool pb_key(pb_t *b, uint32_t *field, uint32_t *wire)
{
    if (b->p >= b->end) return false;
    uint64_t k;
    if (!pb_varint(b, &k)) return false;
    *field = (uint32_t)(k >> 3);
    *wire = (uint32_t)(k & 7);
    return true;
}

static bool pb_len(pb_t *b, pb_t *sub)
{
    uint64_t n;
    if (!pb_varint(b, &n) || (uint64_t)(b->end - b->p) < n) return false;
    sub->p = b->p;
    sub->end = b->p + n;
    b->p += n;
    return true;
}

static bool pb_skip(pb_t *b, uint32_t wire)
{
    uint64_t v;
    pb_t sub;
    switch (wire) {
    case 0: return pb_varint(b, &v);
    case 1: if (b->end - b->p < 8) return false; b->p += 8; return true;
    case 2: return pb_len(b, &sub);
    case 5: if (b->end - b->p < 4) return false; b->p += 4; return true;
    default: return false;
    }
}

static void pb_str(const pb_t *s, char *dst, size_t cap)
{
    size_t n = (size_t)(s->end - s->p);
    if (n >= cap) n = cap - 1;
    memcpy(dst, s->p, n);
    dst[n] = '\0';
}

static bool decode_submsg_strings(pb_t m, uint32_t want_field, char *dst, size_t cap)
{
    uint32_t f, w;
    while (pb_key(&m, &f, &w)) {
        if (f == want_field && w == 2) {
            pb_t s;
            if (!pb_len(&m, &s)) return false;
            pb_str(&s, dst, cap);
            return true;
        }
        if (!pb_skip(&m, w)) return false;
    }
    return false;
}

static bool decode_detail(pb_t d, hud_cot_event_t *ev)
{
    uint32_t f, w;
    while (pb_key(&d, &f, &w)) {
        if (w == 2 && (f == 2 || f == 3 || f == 7)) {
            pb_t sub;
            if (!pb_len(&d, &sub)) return false;
            if (f == 2) { /* Contact: endpoint=1, callsign=2 */
                decode_submsg_strings(sub, 2, ev->callsign, sizeof(ev->callsign));
            } else if (f == 3) { /* Group: name=1, role=2 */
                decode_submsg_strings(sub, 1, ev->team, sizeof(ev->team));
            } else { /* Track: speed=1, course=2 (doubles) */
                uint32_t tf, tw;
                while (pb_key(&sub, &tf, &tw)) {
                    double v;
                    if (tw == 1 && pb_double(&sub, &v)) {
                        if (tf == 1) ev->speed_mps = (float)v;
                        if (tf == 2) ev->course_deg = (float)v;
                        ev->has_track = true;
                    } else if (!pb_skip(&sub, tw)) {
                        break;
                    }
                }
            }
        } else if (!pb_skip(&d, w)) {
            return false;
        }
    }
    return true;
}

static bool decode_cot_event(pb_t e, hud_cot_event_t *ev)
{
    uint32_t f, w;
    bool have_lat = false, have_lon = false;
    while (pb_key(&e, &f, &w)) {
        if (w == 2 && (f == 1 || f == 5 || f == 15)) {
            pb_t s;
            if (!pb_len(&e, &s)) return false;
            if (f == 1) pb_str(&s, ev->type, sizeof(ev->type));
            else if (f == 5) pb_str(&s, ev->uid, sizeof(ev->uid));
            else decode_detail(s, ev);
        } else if (w == 0 && (f == 6 || f == 7 || f == 8)) {
            uint64_t v;
            if (!pb_varint(&e, &v)) return false;
            if (f == 6) ev->time_ms = (int64_t)v;
            if (f == 7) ev->start_ms = (int64_t)v;
            if (f == 8) ev->stale_ms = (int64_t)v;
        } else if (w == 1 && f >= 10 && f <= 14) {
            double v;
            if (!pb_double(&e, &v)) return false;
            switch (f) {
            case 10: ev->lat = v; have_lat = true; break;
            case 11: ev->lon = v; have_lon = true; break;
            case 12: ev->hae = v > 9999990.0 ? 0.0 : v; break;
            case 13: ev->ce = (float)v; break;
            default: ev->le = (float)v; break;
            }
        } else if (!pb_skip(&e, w)) {
            return false;
        }
    }
    return ev->uid[0] && have_lat && have_lon;
}

bool hud_takproto_decode_message(const uint8_t *buf, size_t len, hud_cot_event_t *out)
{
    memset(out, 0, sizeof(*out));
    out->ce = out->le = 9999999.0f;
    pb_t m = {buf, buf + len};
    uint32_t f, w;
    bool ok = false;
    while (pb_key(&m, &f, &w)) {
        if (f == 2 && w == 2) { /* TakMessage.cotEvent */
            pb_t e;
            if (!pb_len(&m, &e)) return false;
            ok = decode_cot_event(e, out);
        } else if (!pb_skip(&m, w)) {
            return false;
        }
    }
    if (!ok) return false;
    if (!out->callsign[0]) {
        memcpy(out->callsign, out->uid, sizeof(out->callsign) - 1);
        out->callsign[sizeof(out->callsign) - 1] = '\0';
    }
    out->affil = hud_cot_affil_from_type(out->type);
    out->dim = hud_cot_dim_from_type(out->type);
    return true;
}

bool hud_takproto_is_mesh(const uint8_t *buf, size_t len)
{
    return len > 3 && buf[0] == 0xBF && buf[1] == 0x01 && buf[2] == 0xBF;
}

bool hud_takproto_decode_mesh(const uint8_t *buf, size_t len, hud_cot_event_t *out)
{
    if (!hud_takproto_is_mesh(buf, len)) return false;
    return hud_takproto_decode_message(buf + 3, len - 3, out);
}
