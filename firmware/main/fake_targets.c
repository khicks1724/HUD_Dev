/*
 * fake_targets.c - stage-2 bring-up: synthetic TAK units around the
 * observer so the projection can be checked by rotating the device, with no
 * network at all. Same scenario as tools/sim_server.py and the web simulator.
 */
#include "fake_targets.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void offset_lla(const hud_lla_t *o, double e, double n, double u, hud_lla_t *out)
{
    const double lat0 = o->lat_deg * M_PI / 180.0;
    const double m_lat = 111132.92 - 559.82 * cos(2 * lat0) + 1.175 * cos(4 * lat0);
    const double m_lon = 111412.84 * cos(lat0) - 93.5 * cos(3 * lat0);
    out->lat_deg = o->lat_deg + n / m_lat;
    out->lon_deg = o->lon_deg + e / m_lon;
    out->hae_m = o->hae_m + u;
}

static void emit(const hud_lla_t *origin, const char *uid, const char *cs, const char *type, double e, double n,
                 double u, float course, float speed)
{
    hud_cot_event_t ev = {0};
    hud_lla_t p;
    offset_lla(origin, e, n, u, &p);
    snprintf(ev.uid, sizeof(ev.uid), "%s", uid);
    snprintf(ev.callsign, sizeof(ev.callsign), "%s", cs);
    snprintf(ev.type, sizeof(ev.type), "%s", type);
    ev.lat = p.lat_deg;
    ev.lon = p.lon_deg;
    ev.hae = p.hae_m;
    ev.has_track = true;
    ev.course_deg = course;
    ev.speed_mps = speed;
    ev.time_ms = 1;
    ev.stale_ms = 1 + 10000;
    ev.affil = hud_cot_affil_from_type(type);
    ev.dim = hud_cot_dim_from_type(type);
    app_ingest_cot(&ev, "fake");
}

static void fake_task(void *arg)
{
    (void)arg;
    hud_lla_t origin = {0};
    bool have_origin = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (g_cfg.man_valid) {
            app_set_own(g_cfg.man_lat, g_cfg.man_lon, g_cfg.man_hae, POS_MANUAL);
        }
        if (!g_cfg.fake_targets) {
            continue;
        }
        app_lock();
        const bool own_ok = g_app.own_valid;
        const hud_lla_t own = g_app.own;
        app_unlock();
        if (!own_ok) {
            app_set_own(g_cfg.man_lat, g_cfg.man_lon, g_cfg.man_hae, POS_FAKE);
            continue;
        }
        if (!have_origin) {
            origin = own; /* scenario is anchored where we first were */
            have_origin = true;
        }
        const double t = app_mono_ms() / 1000.0;
        /* Patrol walking a 150 m circle 350 m north */
        const double a = t * 1.4 / 150.0;
        emit(&origin, "SIM-ALPHA1", "ALPHA1", "a-f-G-U-C-I", 150 * sin(a), 350 + 150 * cos(a), 0,
             (float)fmod(a * 180 / M_PI + 90, 360), 1.4f);
        /* Vehicle driving east-west 600 m out, bearing ~320 */
        const double x = -385 + 300 * sin(t / 40.0);
        emit(&origin, "SIM-BRAVO3", "BRAVO3", "a-f-G-E-V", x, 460, 0, cos(t / 40.0) > 0 ? 90.0f : 270.0f, 7.5f);
        /* UAV orbit, 1.5 km north-east at 450 m AGL */
        const double b = t * 25.0 / 600.0;
        emit(&origin, "SIM-UAV12", "UAV12", "a-f-A-M-F-Q", 1100 + 600 * cos(b), 1100 + 600 * sin(b), 450,
             (float)fmod(360.0 - fmod(b * 180 / M_PI, 360.0), 360.0), 25.0f);
        /* Static hostile and unknown contacts */
        emit(&origin, "SIM-H1", "TGT-H1", "a-h-G-U-C", 60, 1400, 8, 0, 0);
        emit(&origin, "SIM-N1", "CIV", "a-n-G", -900, 300, 0, 0, 0);
        emit(&origin, "SIM-U1", "UNK", "a-u-G", 900, -700, 0, 0, 0);
    }
}

void fake_targets_start(void)
{
    xTaskCreatePinnedToCore(fake_task, "fake", 4096, NULL, 3, NULL, 0);
}
