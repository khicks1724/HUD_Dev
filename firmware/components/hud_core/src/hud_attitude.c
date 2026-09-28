#include "hud_attitude.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RADF ((float)M_PI / 180.0f)
#define RAD2DEGF (180.0f / (float)M_PI)

hud_quat_t hud_quat_identity(void)
{
    return (hud_quat_t){1.0f, 0.0f, 0.0f, 0.0f};
}

hud_quat_t hud_quat_mul(hud_quat_t a, hud_quat_t b)
{
    return (hud_quat_t){
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
    };
}

hud_quat_t hud_quat_conj(hud_quat_t q)
{
    return (hud_quat_t){q.w, -q.x, -q.y, -q.z};
}

hud_quat_t hud_quat_normalize(hud_quat_t q)
{
    float n = sqrtf(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-9f) {
        return hud_quat_identity();
    }
    n = 1.0f / n;
    return (hud_quat_t){q.w * n, q.x * n, q.y * n, q.z * n};
}

hud_quat_t hud_quat_from_axis_angle(float ax, float ay, float az, float angle_rad)
{
    const float s = sinf(angle_rad * 0.5f);
    return (hud_quat_t){cosf(angle_rad * 0.5f), ax * s, ay * s, az * s};
}

hud_vec3f_t hud_quat_rotate(hud_quat_t q, hud_vec3f_t v)
{
    /* v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z) */
    const float tx = 2.0f * (q.y * v.z - q.z * v.y);
    const float ty = 2.0f * (q.z * v.x - q.x * v.z);
    const float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return (hud_vec3f_t){
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

hud_vec3f_t hud_quat_rotate_inv(hud_quat_t q, hud_vec3f_t v)
{
    return hud_quat_rotate(hud_quat_conj(q), v);
}

hud_quat_t hud_quat_from_euler(const hud_euler_t *e)
{
    const hud_quat_t qz = hud_quat_from_axis_angle(0, 0, 1, -e->heading_deg * DEG2RADF);
    const hud_quat_t qx = hud_quat_from_axis_angle(1, 0, 0, e->pitch_deg * DEG2RADF);
    const hud_quat_t qy = hud_quat_from_axis_angle(0, 1, 0, e->roll_deg * DEG2RADF);
    return hud_quat_normalize(hud_quat_mul(hud_quat_mul(qz, qx), qy));
}

void hud_quat_to_euler(hud_quat_t q, hud_euler_t *out)
{
    const hud_vec3f_t f = hud_quat_rotate(q, (hud_vec3f_t){0, 1, 0});
    const hud_vec3f_t r = hud_quat_rotate(q, (hud_vec3f_t){1, 0, 0});
    const hud_vec3f_t u = hud_quat_rotate(q, (hud_vec3f_t){0, 0, 1});
    float fz = f.z;
    if (fz > 1.0f) fz = 1.0f;
    if (fz < -1.0f) fz = -1.0f;

    float hdg = atan2f(f.x, f.y) * RAD2DEGF;
    if (hdg < 0) hdg += 360.0f;
    out->heading_deg = hdg;
    out->pitch_deg = asinf(fz) * RAD2DEGF;
    out->roll_deg = atan2f(-r.z, u.z) * RAD2DEGF;
}

hud_quat_t hud_quat_add_heading(hud_quat_t q, float delta_deg)
{
    const hud_quat_t rz = hud_quat_from_axis_angle(0, 0, 1, -delta_deg * DEG2RADF);
    return hud_quat_normalize(hud_quat_mul(rz, q));
}

/* ---------- Mahony ---------- */

static hud_vec3f_t v_norm(hud_vec3f_t v, int *ok)
{
    const float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (n < 1e-9f) {
        *ok = 0;
        return v;
    }
    *ok = 1;
    return (hud_vec3f_t){v.x / n, v.y / n, v.z / n};
}

static hud_vec3f_t v_cross(hud_vec3f_t a, hud_vec3f_t b)
{
    return (hud_vec3f_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void hud_ahrs_init(hud_ahrs_t *ahrs, float kp, float ki)
{
    ahrs->q = hud_quat_identity();
    ahrs->kp = kp;
    ahrs->ki = ki;
    ahrs->bias_i = (hud_vec3f_t){0, 0, 0};
}

void hud_ahrs_update(hud_ahrs_t *ahrs, hud_vec3f_t gyro, const hud_vec3f_t *accel,
                     const hud_vec3f_t *mag, float dt)
{
    hud_vec3f_t err = {0, 0, 0};
    int ok = 0;

    if (accel) {
        const hud_vec3f_t a = v_norm(*accel, &ok);
        if (ok) {
            /* Estimated "up" in the body frame vs measured specific force. */
            const hud_vec3f_t up_b = hud_quat_rotate_inv(ahrs->q, (hud_vec3f_t){0, 0, 1});
            const hud_vec3f_t e = v_cross(a, up_b);
            err.x += e.x;
            err.y += e.y;
            err.z += e.z;
        }
    }

    if (mag) {
        const hud_vec3f_t m = v_norm(*mag, &ok);
        if (ok) {
            /* Reference field: rotate into world, flatten the horizontal part
             * onto North (+y), keep the vertical component. */
            const hud_vec3f_t h = hud_quat_rotate(ahrs->q, m);
            const hud_vec3f_t b = {0.0f, sqrtf(h.x * h.x + h.y * h.y), h.z};
            const hud_vec3f_t w = hud_quat_rotate_inv(ahrs->q, b);
            const hud_vec3f_t e = v_cross(m, w);
            err.x += e.x;
            err.y += e.y;
            err.z += e.z;
        }
    }

    if (ahrs->ki > 0.0f) {
        ahrs->bias_i.x += ahrs->ki * err.x * dt;
        ahrs->bias_i.y += ahrs->ki * err.y * dt;
        ahrs->bias_i.z += ahrs->ki * err.z * dt;
    }

    const hud_vec3f_t w = {
        gyro.x + ahrs->bias_i.x + ahrs->kp * err.x,
        gyro.y + ahrs->bias_i.y + ahrs->kp * err.y,
        gyro.z + ahrs->bias_i.z + ahrs->kp * err.z,
    };

    /* q_dot = 0.5 * q (x) (0, w) */
    const hud_quat_t dq = hud_quat_mul(ahrs->q, (hud_quat_t){0, w.x, w.y, w.z});
    ahrs->q.w += 0.5f * dq.w * dt;
    ahrs->q.x += 0.5f * dq.x * dt;
    ahrs->q.y += 0.5f * dq.y * dt;
    ahrs->q.z += 0.5f * dq.z * dt;
    ahrs->q = hud_quat_normalize(ahrs->q);
}

void hud_ahrs_nudge_heading(hud_ahrs_t *ahrs, float true_heading_deg, float gain)
{
    hud_euler_t e;
    hud_quat_to_euler(ahrs->q, &e);
    float d = true_heading_deg - e.heading_deg;
    while (d > 180.0f) d -= 360.0f;
    while (d <= -180.0f) d += 360.0f;
    ahrs->q = hud_quat_add_heading(ahrs->q, d * gain);
}
