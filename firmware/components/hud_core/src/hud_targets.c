#include "hud_targets.h"

#include <string.h>

#define MIN_LIFETIME_MS 5000
#define MAX_LIFETIME_MS (60 * 60 * 1000)
#define DEFAULT_LIFETIME_MS 60000

static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 16777619u;
    }
    return h;
}

void hud_targets_init(hud_targets_t *db)
{
    memset(db, 0, sizeof(*db));
}

bool hud_targets_type_is_displayable(const char *type)
{
    /* "a-" atoms are units/equipment. Also allow b-m-p (map points / waypoints). */
    return type && ((type[0] == 'a' && type[1] == '-') || strncmp(type, "b-m-p", 5) == 0);
}

hud_target_t *hud_targets_find(hud_targets_t *db, const char *uid)
{
    const uint32_t h = fnv1a(uid);
    for (int i = 0; i < HUD_MAX_TARGETS; i++) {
        hud_target_t *t = &db->items[i];
        if (t->used && t->uid_hash == h && strcmp(t->ev.uid, uid) == 0) {
            return t;
        }
    }
    return NULL;
}

static int64_t lifetime_ms(const hud_cot_event_t *ev, int64_t now_utc_ms)
{
    int64_t life = DEFAULT_LIFETIME_MS;
    if (ev->stale_ms && ev->time_ms) {
        life = ev->stale_ms - ev->time_ms;
    } else if (ev->stale_ms && now_utc_ms) {
        life = ev->stale_ms - now_utc_ms;
    }
    if (life < MIN_LIFETIME_MS) life = MIN_LIFETIME_MS;
    if (life > MAX_LIFETIME_MS) life = MAX_LIFETIME_MS;
    return life;
}

hud_target_t *hud_targets_upsert(hud_targets_t *db, const hud_cot_event_t *ev, int64_t now_mono_ms,
                                 int64_t now_utc_ms)
{
    if (!hud_targets_type_is_displayable(ev->type)) {
        return NULL;
    }
    hud_target_t *t = hud_targets_find(db, ev->uid);
    if (!t) {
        for (int i = 0; i < HUD_MAX_TARGETS; i++) {
            if (!db->items[i].used) {
                t = &db->items[i];
                break;
            }
        }
        if (!t) {
            /* Full: evict the entry closest to expiry. */
            hud_target_t *victim = &db->items[0];
            for (int i = 1; i < HUD_MAX_TARGETS; i++) {
                if (db->items[i].expires_mono_ms < victim->expires_mono_ms) {
                    victim = &db->items[i];
                }
            }
            t = victim;
            db->dropped_full++;
            db->count--;
        }
        memset(t, 0, sizeof(*t));
        t->used = true;
        t->uid_hash = fnv1a(ev->uid);
        db->count++;
    }
    t->ev = *ev;
    t->updated_mono_ms = now_mono_ms;
    t->expires_mono_ms = now_mono_ms + lifetime_ms(ev, now_utc_ms);
    return t;
}

int hud_targets_prune(hud_targets_t *db, int64_t now_mono_ms)
{
    int removed = 0;
    for (int i = 0; i < HUD_MAX_TARGETS; i++) {
        hud_target_t *t = &db->items[i];
        if (t->used && now_mono_ms >= t->expires_mono_ms) {
            t->used = false;
            db->count--;
            removed++;
        }
    }
    return removed;
}
