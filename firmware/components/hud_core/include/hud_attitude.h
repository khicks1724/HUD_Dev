/*
 * hud_attitude.h - quaternion attitude + Mahony AHRS.
 *
 * Body frame (the "HUD frame"):
 *     x = right, y = forward (boresight, through the prism), z = up
 * World frame: ENU (x = East, y = North, z = Up).
 *
 * The quaternion q maps body vectors into the world frame (v_w = q v_b q*).
 * The identity quaternion therefore means: level, looking due north.
 *
 * Euler convention used everywhere in this project (firmware, web page,
 * Python tools):
 *     heading  psi   degrees clockwise from true north
 *     pitch    theta degrees, nose up positive
 *     roll     phi   degrees, right side down positive
 *     R = Rz(-psi) * Rx(theta) * Ry(phi)
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float w, x, y, z;
} hud_quat_t;

typedef struct {
    float x, y, z;
} hud_vec3f_t;

typedef struct {
    float heading_deg;
    float pitch_deg;
    float roll_deg;
} hud_euler_t;

hud_quat_t hud_quat_identity(void);
hud_quat_t hud_quat_mul(hud_quat_t a, hud_quat_t b);
hud_quat_t hud_quat_conj(hud_quat_t q);
hud_quat_t hud_quat_normalize(hud_quat_t q);
hud_quat_t hud_quat_from_axis_angle(float ax, float ay, float az, float angle_rad);

/* body -> world */
hud_vec3f_t hud_quat_rotate(hud_quat_t q, hud_vec3f_t v);
/* world -> body */
hud_vec3f_t hud_quat_rotate_inv(hud_quat_t q, hud_vec3f_t v);

hud_quat_t hud_quat_from_euler(const hud_euler_t *e);
void hud_quat_to_euler(hud_quat_t q, hud_euler_t *out);

/* Rotate the attitude about world Up so heading increases by delta_deg. */
hud_quat_t hud_quat_add_heading(hud_quat_t q, float delta_deg);

/* ---------- Mahony AHRS ---------- */

typedef struct {
    hud_quat_t q;
    float kp;           /* proportional gain (accel/mag correction) */
    float ki;           /* integral gain (gyro bias estimation) */
    hud_vec3f_t bias_i; /* integral term, rad/s */
} hud_ahrs_t;

void hud_ahrs_init(hud_ahrs_t *ahrs, float kp, float ki);

/*
 * gyro   : rad/s in the body frame
 * accel  : specific force in the body frame, any unit (normalised); at rest it
 *          points UP. Pass NULL to skip the tilt correction.
 * mag    : magnetic field in body frame, any unit; NULL when no magnetometer.
 *          Without a magnetometer yaw is gyro-only and drifts; feed an external
 *          heading with hud_ahrs_nudge_heading().
 * dt     : seconds
 */
void hud_ahrs_update(hud_ahrs_t *ahrs, hud_vec3f_t gyro, const hud_vec3f_t *accel,
                     const hud_vec3f_t *mag, float dt);

/*
 * Complementary heading correction from an external source (phone compass,
 * boresight alignment, GNSS course). gain in 0..1; 1.0 snaps immediately.
 */
void hud_ahrs_nudge_heading(hud_ahrs_t *ahrs, float true_heading_deg, float gain);

#ifdef __cplusplus
}
#endif
