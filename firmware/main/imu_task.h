/*
 * imu_task.h - QMI8658 (+ optional MMC5983MA) -> Mahony AHRS -> HUD attitude.
 */
#pragma once

#include <stdbool.h>

#include "hud_attitude.h"

typedef enum { HDG_GYRO = 0, HDG_MAG, HDG_PHONE, HDG_BORE, HDG_COURSE } hdg_src_t;

void imu_task_start(void);

/* Latest attitude (world_from_body) including boresight trim. Thread safe. */
hud_quat_t imu_get_quat(void);
hdg_src_t imu_heading_source(void);
const char *imu_heading_source_name(void);
bool imu_ok(void);

/* External heading (true, degrees). gain 1.0 snaps; small gains blend. */
void imu_set_heading(float true_heading_deg, float gain, hdg_src_t src);

/* Two-pose mount calibration (docs/CALIBRATION.md):
 *   1. hold the HUD level, looking forward      -> imu_cal_capture_level()
 *   2. pitch the nose up ~30-60 deg              -> imu_cal_capture_nose_up()
 * computes g_cfg.mount (sensor -> HUD body). */
void imu_cal_capture_level(void);
bool imu_cal_capture_nose_up(void);

/* Level trim: records current pitch/roll as the zero reference. */
void imu_trim_level(void);

/* Raw (sensor frame) values for the console "imu" command. */
void imu_get_raw(float acc[3], float gyr[3], float mag[3]);
