/*
 * Host-side unit tests for components/hud_core. No ESP-IDF needed:
 *
 *   gcc -std=c11 -O2 -I../components/hud_core/include test_main.c \
 *       ../components/hud_core/src/<all>.c -lm -o hud_tests && ./hud_tests
 *
 * or run_tests.sh / run_tests.ps1 in this folder.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hud_attitude.h"
#include "hud_cot.h"
#include "hud_geo.h"
#include "hud_nmea.h"
#include "hud_projection.h"
#include "hud_takproto.h"
#include "hud_targets.h"
#include "vectors.h"

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) {                                       \
            g_pass++;                                     \
        } else {                                          \
            g_fail++;                                     \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

#define NEAR(a, b, tol) (fabs((double)(a) - (double)(b)) <= (tol))

static void test_vectors(void)
{
    hud_proj_cfg_t cfg;
    hud_proj_cfg_from_fov(&cfg, 240, 240, VEC_HFOV, VEC_VFOV);
    for (size_t i = 0; i < VEC_COUNT; i++) {
        const vec_case_t *c = &VEC_CASES[i];
        hud_lla_t o = {c->obs[0], c->obs[1], c->obs[2]}, t = {c->tgt[0], c->tgt[1], c->tgt[2]};
        hud_vec3d_t enu;
        hud_lla_to_enu(&o, &t, &enu);
        CHECK(NEAR(enu.x, c->enu[0], 1e-6) && NEAR(enu.y, c->enu[1], 1e-6) && NEAR(enu.z, c->enu[2], 1e-6),
              "case %zu enu (%f %f %f) vs (%f %f %f)", i, enu.x, enu.y, enu.z, c->enu[0], c->enu[1], c->enu[2]);

        hud_polar_t p;
        hud_enu_to_polar(&enu, &p);
        CHECK(NEAR(p.bearing_deg, c->bearing, 1e-9) && NEAR(p.elevation_deg, c->elevation, 1e-9) &&
                  NEAR(p.range_m, c->range, 1e-6),
              "case %zu polar", i);

        hud_euler_t e = {c->att[0], c->att[1], c->att[2]};
        hud_quat_t q = hud_quat_from_euler(&e);
        CHECK(NEAR(q.w, c->q[0], 1e-5) && NEAR(q.x, c->q[1], 1e-5) && NEAR(q.y, c->q[2], 1e-5) &&
                  NEAR(q.z, c->q[3], 1e-5),
              "case %zu quat (%f %f %f %f) vs (%f %f %f %f)", i, q.w, q.x, q.y, q.z, c->q[0], c->q[1], c->q[2],
              c->q[3]);

        hud_euler_t back;
        hud_quat_to_euler(q, &back);
        float dh = back.heading_deg - e.heading_deg;
        while (dh > 180) dh -= 360;
        while (dh < -180) dh += 360;
        CHECK(fabsf(dh) < 0.01f && NEAR(back.pitch_deg, e.pitch_deg, 0.01) && NEAR(back.roll_deg, e.roll_deg, 0.01),
              "case %zu euler roundtrip", i);

        hud_proj_t pr;
        hud_project(q, (hud_vec3f_t){(float)enu.x, (float)enu.y, (float)enu.z}, &cfg, &pr);
        CHECK(pr.in_front == (c->in_front != 0), "case %zu in_front", i);
        if (c->in_front) {
            /* float vs double: allow a small pixel error that grows at grazing angles */
            CHECK(NEAR(pr.sx, c->sx, 0.05 + 1e-4 * fabs(c->sx)) && NEAR(pr.sy, c->sy, 0.05 + 1e-4 * fabs(c->sy)),
                  "case %zu proj (%f,%f) vs (%f,%f)", i, pr.sx, pr.sy, c->sx, c->sy);
        }
    }
}

static void test_projection_basics(void)
{
    hud_proj_cfg_t cfg;
    hud_proj_cfg_from_fov(&cfg, 240, 240, 40, 40);
    hud_quat_t q = hud_quat_identity();
    hud_proj_t p;

    hud_project(q, (hud_vec3f_t){0, 1000, 0}, &cfg, &p);
    CHECK(p.on_screen && NEAR(p.sx, cfg.cx, 1e-3) && NEAR(p.sy, cfg.cy, 1e-3), "dead ahead at centre");

    /* 20 deg right == half hfov == right edge */
    hud_project(q, (hud_vec3f_t){1000 * sinf(0.349066f), 1000 * cosf(0.349066f), 0}, &cfg, &p);
    CHECK(NEAR(p.sx, cfg.cx + 120, 0.01), "20deg right at edge: %f", p.sx);

    /* above -> smaller sy */
    hud_project(q, (hud_vec3f_t){0, 1000, 50}, &cfg, &p);
    CHECK(p.sy < cfg.cy, "above is up");

    /* behind -> not in front, cue at bottom */
    hud_project(q, (hud_vec3f_t){0, -1000, 0}, &cfg, &p);
    CHECK(!p.in_front && p.edge_y > cfg.cy, "behind cue at bottom");

    /* hard right, off screen -> cue on right edge */
    hud_project(q, (hud_vec3f_t){1000, 100, 0}, &cfg, &p);
    CHECK(!p.on_screen && p.edge_x > cfg.width - 1 - cfg.edge_margin_px - 1, "right cue %f", p.edge_x);

    /* facing east, east target at centre */
    hud_euler_t e = {90, 0, 0};
    q = hud_quat_from_euler(&e);
    hud_project(q, (hud_vec3f_t){1000, 0, 0}, &cfg, &p);
    CHECK(p.on_screen && NEAR(p.sx, cfg.cx, 0.01), "east facing");

    /* roll right: a target straight ahead but above should move right on screen
     * (world tilts left relative to a right-rolled head, so "up" moves to the right). */
    e = (hud_euler_t){0, 0, 20};
    q = hud_quat_from_euler(&e);
    hud_project(q, (hud_vec3f_t){0, 1000, 100}, &cfg, &p);
    CHECK(p.sx < cfg.cx, "roll right: world-up point appears left of centre (%f)", p.sx);
}

static void test_ahrs(void)
{
    hud_ahrs_t a;
    hud_ahrs_init(&a, 2.0f, 0.0f);
    /* Start tilted 30 deg; accel says level. Should converge to level. */
    hud_euler_t e0 = {0, 30, 0};
    a.q = hud_quat_from_euler(&e0);
    for (int i = 0; i < 2000; i++) {
        hud_ahrs_update(&a, (hud_vec3f_t){0, 0, 0}, &(hud_vec3f_t){0, 0, 1}, NULL, 0.005f);
    }
    hud_euler_t e;
    hud_quat_to_euler(a.q, &e);
    CHECK(fabsf(e.pitch_deg) < 0.5f && fabsf(e.roll_deg) < 0.5f, "ahrs converges level (%f,%f)", e.pitch_deg,
          e.roll_deg);

    /* Yaw rotation: +z gyro in body (counter-clockwise from above) lowers heading. */
    hud_ahrs_init(&a, 0.0f, 0.0f);
    for (int i = 0; i < 100; i++) {
        hud_ahrs_update(&a, (hud_vec3f_t){0, 0, 0.1745329f}, NULL, NULL, 0.01f); /* 10 deg/s * 1 s */
    }
    hud_quat_to_euler(a.q, &e);
    CHECK(NEAR(e.heading_deg, 350.0, 0.2), "gyro yaw integrates: %f", e.heading_deg);

    /* Magnetometer pulls heading to north. Field: north + down (northern hemisphere). */
    hud_ahrs_init(&a, 2.0f, 0.0f);
    hud_euler_t e1 = {45, 0, 0};
    a.q = hud_quat_from_euler(&e1);
    for (int i = 0; i < 4000; i++) {
        /* body sees field rotated by the TRUE attitude (level, heading 0) => world field */
        hud_vec3f_t m = {0.0f, 0.5f, -0.3f};
        hud_ahrs_update(&a, (hud_vec3f_t){0, 0, 0}, &(hud_vec3f_t){0, 0, 1}, &m, 0.005f);
    }
    hud_quat_to_euler(a.q, &e);
    float h = e.heading_deg > 180 ? e.heading_deg - 360 : e.heading_deg;
    CHECK(fabsf(h) < 1.0f, "mag converges to north: %f", e.heading_deg);

    hud_ahrs_nudge_heading(&a, 123.0f, 1.0f);
    hud_quat_to_euler(a.q, &e);
    CHECK(NEAR(e.heading_deg, 123.0, 0.05), "nudge heading snap: %f", e.heading_deg);
}

static int g_cb_count;
static hud_cot_event_t g_last;
static void on_ev(const hud_cot_event_t *ev, void *ctx)
{
    (void)ctx;
    g_cb_count++;
    g_last = *ev;
}

static const char *SAMPLE =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<event version=\"2.0\" uid=\"ANDROID-abc123\" type=\"a-f-G-U-C\" how=\"m-g\" "
    "time=\"2026-09-28T14:44:09.000Z\" start=\"2026-09-28T14:44:09.000Z\" stale=\"2026-09-28T14:50:09.500Z\">"
    "<point lat=\"36.5967\" lon=\"-121.875\" hae=\"25.5\" ce=\"4.9\" le=\"9999999.0\"/>"
    "<detail><contact endpoint=\"*:-1:stcp\" callsign=\"ALPHA &amp; 1\"/>"
    "<__group name=\"Cyan\" role=\"Team Member\"/>"
    "<track course='271.5' speed='1.25'/></detail></event>";

static void test_cot(void)
{
    hud_cot_event_t ev;
    CHECK(hud_cot_parse_event(SAMPLE, strlen(SAMPLE), &ev), "parse sample");
    CHECK(strcmp(ev.uid, "ANDROID-abc123") == 0, "uid %s", ev.uid);
    CHECK(strcmp(ev.callsign, "ALPHA & 1") == 0, "callsign %s", ev.callsign);
    CHECK(strcmp(ev.team, "Cyan") == 0, "team");
    CHECK(NEAR(ev.lat, 36.5967, 1e-9) && NEAR(ev.lon, -121.875, 1e-9) && NEAR(ev.hae, 25.5, 1e-9), "point");
    CHECK(ev.has_track && NEAR(ev.course_deg, 271.5, 1e-4) && NEAR(ev.speed_mps, 1.25, 1e-4), "track");
    CHECK(ev.affil == HUD_AFFIL_FRIEND && ev.dim == HUD_DIM_GROUND, "affil/dim");
    CHECK(ev.stale_ms - ev.time_ms == 360500, "lifetime %lld", (long long)(ev.stale_ms - ev.time_ms));
    CHECK(ev.time_ms == 1790606649000LL, "epoch %lld", (long long)ev.time_ms);

    char tbuf[25];
    hud_cot_format_time(1790606649123LL, tbuf);
    CHECK(strcmp(tbuf, "2026-09-28T14:44:09.123Z") == 0, "format %s", tbuf);

    CHECK(hud_cot_affil_from_type("a-h-A-M-F") == HUD_AFFIL_HOSTILE, "hostile");
    CHECK(hud_cot_dim_from_type("a-h-A-M-F") == HUD_DIM_AIR, "air");
    CHECK(hud_cot_affil_from_type("a-n-G") == HUD_AFFIL_NEUTRAL, "neutral");
    CHECK(hud_cot_affil_from_type("b-m-p-s-p-i") == HUD_AFFIL_UNKNOWN, "non-atom");

    /* Stream: two events split at awkward boundaries, junk in between. */
    static hud_cot_stream_t s;
    hud_cot_stream_init(&s);
    g_cb_count = 0;
    char stream[4096];
    snprintf(stream, sizeof(stream), "garbage%s\n\n%s", SAMPLE, SAMPLE);
    const size_t n = strlen(stream);
    for (size_t i = 0; i < n; i += 7) {
        const size_t chunk = (n - i) < 7 ? (n - i) : 7;
        hud_cot_stream_feed(&s, stream + i, chunk, on_ev, NULL);
    }
    CHECK(g_cb_count == 2, "stream events %d", g_cb_count);
    CHECK(strcmp(g_last.uid, "ANDROID-abc123") == 0, "stream uid");

    /* Round-trip our own SA builder through the parser. */
    char out[1024];
    const int len = hud_cot_build_sa(out, sizeof(out), "HUD-001", "HUD-001", NULL, 36.1, -121.2, 12.0, 5.0f,
                                     90.0f, 0.0f, 1790606649000LL, 60);
    CHECK(len > 0, "build sa");
    CHECK(hud_cot_parse_event(out, (size_t)len, &ev) && strcmp(ev.uid, "HUD-001") == 0 && NEAR(ev.lat, 36.1, 1e-6),
          "sa roundtrip");
    CHECK(hud_cot_build_ping(out, sizeof(out), "HUD-001", 1790606649000LL) > 0, "ping");
}

static void test_targets(void)
{
    static hud_targets_t db;
    hud_targets_init(&db);
    hud_cot_event_t ev;
    hud_cot_parse_event(SAMPLE, strlen(SAMPLE), &ev);
    CHECK(hud_targets_upsert(&db, &ev, 1000, 0) != NULL, "insert");
    CHECK(hud_targets_upsert(&db, &ev, 2000, 0) != NULL && db.count == 1, "update in place");
    CHECK(hud_targets_find(&db, "ANDROID-abc123") != NULL, "find");
    CHECK(hud_targets_prune(&db, 2000 + 360000) == 0, "not yet stale");
    CHECK(hud_targets_prune(&db, 2000 + 360500) == 1 && db.count == 0, "stale removed");

    strcpy(ev.type, "t-x-c-t");
    CHECK(hud_targets_upsert(&db, &ev, 0, 0) == NULL, "ping ignored");

    for (int i = 0; i < HUD_MAX_TARGETS + 5; i++) {
        hud_cot_parse_event(SAMPLE, strlen(SAMPLE), &ev);
        snprintf(ev.uid, sizeof(ev.uid), "U-%d", i);
        hud_targets_upsert(&db, &ev, i, 0);
    }
    CHECK(db.count == HUD_MAX_TARGETS && db.dropped_full == 5, "full table evicts (%d, %u)", db.count,
          db.dropped_full);
}

static void test_nmea(void)
{
    hud_gnss_t fix;
    memset(&fix, 0, sizeof(fix));
    hud_nmea_reader_t r;
    hud_nmea_reader_init(&r);
    const char *data = "$GNGGA,144409.00,3635.80200,N,12152.50000,W,1,12,0.80,20.0,M,-32.0,M,,*40\r\n"
                       "$GNRMC,144409.00,A,3635.80200,N,12152.50000,W,1.944,90.0,280926,,,A*61\r\n";
    const int n = hud_nmea_feed(&r, data, strlen(data), &fix);
    CHECK(n == 2, "nmea sentences %d (ok %u bad %u)", n, fix.sentences_ok, fix.sentences_bad);
    CHECK(fix.valid && NEAR(fix.lat_deg, 36.5967, 1e-6) && NEAR(fix.lon_deg, -121.875, 1e-6), "nmea pos %f %f",
          fix.lat_deg, fix.lon_deg);
    CHECK(NEAR(fix.hae_m, -12.0, 1e-6) && fix.sats == 12, "nmea alt");
    CHECK(NEAR(fix.speed_mps, 1.0, 0.01) && fix.course_valid && NEAR(fix.course_deg, 90.0, 1e-3), "nmea rmc");
    CHECK(fix.utc_ms == 1790606649000LL, "nmea utc %lld", (long long)fix.utc_ms);
    CHECK(!hud_nmea_parse_sentence("$GNGGA,1*00", &fix), "bad checksum rejected");
}


/* Generated with: python tools/takproto.py */
static const uint8_t PROTO_MESH[] = {0xbf, 0x01, 0xbf, 0x12, 0xa4, 0x01, 0x0a, 0x09, 0x61, 0x2d, 0x68, 0x2d, 0x47, 0x2d, 0x55, 0x2d, 0x43, 0x2a, 0x0e, 0x41, 0x4e, 0x44, 0x52, 0x4f, 0x49, 0x44, 0x2d, 0x70, 0x72, 0x6f, 0x74, 0x6f, 0x31, 0x30, 0xa8, 0xcd, 0xe4, 0xc3, 0x8e, 0x34, 0x38, 0xa8, 0xcd, 0xe4, 0xc3, 0x8e, 0x34, 0x40, 0x88, 0xa2, 0xe8, 0xc3, 0x8e, 0x34, 0x4a, 0x03, 0x6d, 0x2d, 0x67, 0x51, 0xcd, 0xcc, 0xcc, 0xcc, 0xcc, 0x4c, 0x42, 0x40, 0x59, 0x9a, 0x99, 0x99, 0x99, 0x99, 0x79, 0x5e, 0xc0, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x40, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x40, 0x71, 0x00, 0x00, 0x00, 0xe0, 0xcf, 0x12, 0x63, 0x41, 0x7a, 0x40, 0x12, 0x16, 0x0a, 0x09, 0x2a, 0x3a, 0x2d, 0x31, 0x3a, 0x73, 0x74, 0x63, 0x70, 0x12, 0x09, 0x48, 0x4f, 0x53, 0x54, 0x49, 0x4c, 0x45, 0x20, 0x37, 0x1a, 0x12, 0x0a, 0x03, 0x52, 0x65, 0x64, 0x12, 0x0b, 0x54, 0x65, 0x61, 0x6d, 0x20, 0x4d, 0x65, 0x6d, 0x62, 0x65, 0x72, 0x3a, 0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x46, 0x40};

static void test_takproto(void)
{
    hud_cot_event_t ev;
    CHECK(hud_takproto_is_mesh(PROTO_MESH, sizeof(PROTO_MESH)), "mesh header");
    CHECK(hud_takproto_decode_mesh(PROTO_MESH, sizeof(PROTO_MESH), &ev), "decode proto");
    CHECK(strcmp(ev.uid, "ANDROID-proto1") == 0 && strcmp(ev.type, "a-h-G-U-C") == 0, "proto uid/type");
    CHECK(strcmp(ev.callsign, "HOSTILE 7") == 0 && strcmp(ev.team, "Red") == 0, "proto contact/group");
    CHECK(NEAR(ev.lat, 36.6, 1e-9) && NEAR(ev.lon, -121.9, 1e-9) && NEAR(ev.hae, 12.5, 1e-9), "proto point");
    CHECK(ev.has_track && NEAR(ev.course_deg, 45.0, 1e-4) && NEAR(ev.speed_mps, 2.5, 1e-4), "proto track");
    CHECK(ev.stale_ms - ev.time_ms == 60000 && ev.affil == HUD_AFFIL_HOSTILE, "proto times/affil");
    CHECK(!hud_takproto_decode_mesh(PROTO_MESH, 20, &ev), "truncated rejected");
}

int main(void)
{
    test_vectors();
    test_projection_basics();
    test_ahrs();
    test_cot();
    test_targets();
    test_nmea();
    test_takproto();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
