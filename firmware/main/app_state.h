/*
 * app_state.h - state shared between tasks, guarded by one mutex.
 *
 *   net/tak/mesh/fake tasks  --write-->  targets, own position
 *   imu task                 --write-->  attitude (own lock-free copy, see imu_task.h)
 *   render task              --read--->  everything, 30 Hz
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hud_cot.h"
#include "hud_geo.h"
#include "hud_render.h"
#include "hud_targets.h"

typedef enum { POS_NONE = 0, POS_FAKE, POS_MANUAL, POS_TAK, POS_UDP, POS_USB, POS_GNSS } pos_src_t;

typedef struct {
    SemaphoreHandle_t lock;
    hud_targets_t *targets; /* in PSRAM */

    hud_lla_t own;
    bool own_valid;
    pos_src_t pos_src;
    int64_t own_mono_ms;
    float own_course_deg, own_speed_mps;

    int link_state; /* 0 none, 1 wifi, 2 wifi + TAK stream */
    bool time_valid;
    char ip[16];

    uint32_t cot_rx, cot_bad, mesh_rx, udp_rx;

    hud_mode_t mode;
    hud_thermal_mode_t thermal_mode;
    char last_error[48];
} app_state_t;

extern app_state_t g_app;

void app_state_init(void);
static inline void app_lock(void) { xSemaphoreTake(g_app.lock, portMAX_DELAY); }
static inline void app_unlock(void) { xSemaphoreGive(g_app.lock); }

int64_t app_mono_ms(void);
int64_t app_utc_ms(void); /* 0 if wall clock not yet valid */

/* Common sink for CoT from any source (TAK stream, mesh, fake). Handles the
 * "this event is my own phone" case. Takes the lock itself. */
void app_ingest_cot(const hud_cot_event_t *ev, const char *via);

/* Set own position from any source. Takes the lock. */
void app_set_own(double lat, double lon, double hae, pos_src_t src);

const char *app_pos_src_name(pos_src_t s);
void app_set_error(const char *msg);
