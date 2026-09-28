/*
 * hud_targets.h - fixed-size table of tracked TAK units.
 *
 * Expiry is tracked against a local monotonic clock using the event's own
 * (stale - time) lifetime, so the table works before NTP has set the wall
 * clock. Not thread-safe: the firmware wraps it in a mutex.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hud_cot.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HUD_MAX_TARGETS
#define HUD_MAX_TARGETS 256
#endif

typedef struct {
    bool used;
    uint32_t uid_hash;
    hud_cot_event_t ev;
    int64_t updated_mono_ms;
    int64_t expires_mono_ms;
} hud_target_t;

typedef struct {
    hud_target_t items[HUD_MAX_TARGETS];
    int count;
    uint32_t dropped_full;
} hud_targets_t;

void hud_targets_init(hud_targets_t *db);

/*
 * Insert or update from a parsed event. now_mono_ms is a monotonic clock;
 * now_utc_ms may be 0 if the wall clock is not known yet.
 * Returns the slot or NULL (table full / ignored type).
 */
hud_target_t *hud_targets_upsert(hud_targets_t *db, const hud_cot_event_t *ev, int64_t now_mono_ms,
                                 int64_t now_utc_ms);

hud_target_t *hud_targets_find(hud_targets_t *db, const char *uid);

/* Remove expired entries; returns number removed. */
int hud_targets_prune(hud_targets_t *db, int64_t now_mono_ms);

/* Should this CoT type be shown on the HUD? (atoms only; drops pings, chat, etc.) */
bool hud_targets_type_is_displayable(const char *type);

#ifdef __cplusplus
}
#endif
