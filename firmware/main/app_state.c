#include "app_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hud_config.h"

static const char *TAG = "state";

app_state_t g_app;

/* Anything later than 2024-01-01 means SNTP (or GNSS) has set the clock. */
#define VALID_EPOCH_S 1704067200LL

void app_state_init(void)
{
    memset(&g_app, 0, sizeof(g_app));
    g_app.lock = xSemaphoreCreateMutex();
    g_app.targets = heap_caps_calloc(1, sizeof(hud_targets_t), MALLOC_CAP_SPIRAM);
    if (!g_app.targets) {
        g_app.targets = calloc(1, sizeof(hud_targets_t));
    }
    hud_targets_init(g_app.targets);
    g_app.mode = (hud_mode_t)g_cfg.mode;
}

int64_t app_mono_ms(void)
{
    return esp_timer_get_time() / 1000;
}

int64_t app_utc_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < VALID_EPOCH_S) {
        return 0;
    }
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

const char *app_pos_src_name(pos_src_t s)
{
    switch (s) {
    case POS_FAKE: return "FAKE";
    case POS_MANUAL: return "MAN";
    case POS_TAK: return "TAK";
    case POS_UDP: return "UDP";
    case POS_USB: return "USB";
    case POS_GNSS: return "GNSS";
    default: return "NONE";
    }
}

void app_set_error(const char *msg)
{
    app_lock();
    strncpy(g_app.last_error, msg, sizeof(g_app.last_error) - 1);
    g_app.last_error[sizeof(g_app.last_error) - 1] = '\0';
    app_unlock();
}

static int pos_priority(pos_src_t s)
{
    /* Higher wins while fresh. GNSS on the HUD itself beats the phone. */
    switch (s) {
    case POS_GNSS: return 5;
    case POS_TAK: return 4;
    case POS_UDP: return 3;
    case POS_USB: return 4;
    case POS_MANUAL: return 2;
    case POS_FAKE: return 1;
    default: return 0;
    }
}

void app_set_own(double lat, double lon, double hae, pos_src_t src)
{
    const int64_t now = app_mono_ms();
    app_lock();
    const bool current_stale = (now - g_app.own_mono_ms) > 15000;
    if (!g_app.own_valid || current_stale || pos_priority(src) >= pos_priority(g_app.pos_src)) {
        g_app.own = (hud_lla_t){lat, lon, hae};
        g_app.own_valid = true;
        g_app.pos_src = src;
        g_app.own_mono_ms = now;
    }
    app_unlock();
}

void app_ingest_cot(const hud_cot_event_t *ev, const char *via)
{
    /* Our own echo from the server. */
    if (strcmp(ev->uid, g_cfg.hud_uid) == 0) {
        return;
    }
    const bool is_owner = (g_cfg.own_uid[0] && strcmp(ev->uid, g_cfg.own_uid) == 0) ||
                          (g_cfg.own_callsign[0] && strcmp(ev->callsign, g_cfg.own_callsign) == 0);
    if (is_owner) {
        app_set_own(ev->lat, ev->lon, ev->hae, POS_TAK);
        app_lock();
        if (ev->has_track) {
            g_app.own_course_deg = ev->course_deg;
            g_app.own_speed_mps = ev->speed_mps;
        }
        app_unlock();
        ESP_LOGD(TAG, "own position from %s via %s", ev->uid, via);
        return;
    }
    app_lock();
    hud_targets_upsert(g_app.targets, ev, app_mono_ms(), app_utc_ms());
    app_unlock();
}

void app_ingest_track(const char *uid, const char *type, double lat, double lon, double hae, int stale_s,
                      const char *callsign, const char *via)
{
    hud_cot_event_t ev = {0};
    snprintf(ev.uid, sizeof(ev.uid), "%s", uid);
    snprintf(ev.type, sizeof(ev.type), "%s", type);
    snprintf(ev.callsign, sizeof(ev.callsign), "%s", callsign && callsign[0] ? callsign : uid);
    ev.lat = lat;
    ev.lon = lon;
    ev.hae = hae;
    ev.time_ms = 1;
    ev.stale_ms = 1 + (int64_t)(stale_s > 0 ? stale_s : 30) * 1000;
    ev.affil = hud_cot_affil_from_type(ev.type);
    ev.dim = hud_cot_dim_from_type(ev.type);
    app_ingest_cot(&ev, via);
}
