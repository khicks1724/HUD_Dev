#include "imu_task.h"

#include <math.h>
#include <string.h>

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hud_config.h"
#include "mmc5983ma.h"
#include "qmi8658.h"
#include "sdkconfig.h"

static const char *TAG = "imu";

#define IMU_HZ 200
#define BIAS_SAMPLES 400

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static hud_ahrs_t s_ahrs;
static hud_quat_t s_q_out;
static hdg_src_t s_hdg_src = HDG_GYRO;
static bool s_ok;
static float s_raw_acc[3], s_raw_gyr[3], s_raw_mag[3];
static float s_lp_acc[3]; /* low-passed accel, sensor frame, for calibration captures */
static float s_cal_up[3];
static bool s_cal_up_valid;

static hud_vec3f_t mount_apply(const float *m, float x, float y, float z)
{
    return (hud_vec3f_t){
        m[0] * x + m[1] * y + m[2] * z,
        m[3] * x + m[4] * y + m[5] * z,
        m[6] * x + m[7] * y + m[8] * z,
    };
}

static hud_quat_t trim_quat(void)
{
    const hud_euler_t e = {0.0f, g_cfg.bore_pitch_deg, g_cfg.bore_roll_deg};
    return hud_quat_conj(hud_quat_from_euler(&e));
}

hud_quat_t imu_get_quat(void)
{
    taskENTER_CRITICAL(&s_mux);
    const hud_quat_t q = s_q_out;
    taskEXIT_CRITICAL(&s_mux);
    return q;
}

hdg_src_t imu_heading_source(void)
{
    return s_hdg_src;
}

const char *imu_heading_source_name(void)
{
    switch (s_hdg_src) {
    case HDG_MAG: return "MAG";
    case HDG_PHONE: return "PHONE";
    case HDG_BORE: return "BORE";
    case HDG_COURSE: return "COURSE";
    default: return "GYRO";
    }
}

bool imu_ok(void)
{
    return s_ok;
}

void imu_set_heading(float true_heading_deg, float gain, hdg_src_t src)
{
    taskENTER_CRITICAL(&s_mux);
    /* The published quaternion includes the trim; nudge the AHRS state so the
     * published heading lands on the requested value. */
    hud_euler_t out_e, ahrs_e;
    hud_quat_to_euler(s_q_out, &out_e);
    hud_quat_to_euler(s_ahrs.q, &ahrs_e);
    const float delta = true_heading_deg - out_e.heading_deg;
    hud_ahrs_nudge_heading(&s_ahrs, ahrs_e.heading_deg + delta, gain);
    if (s_hdg_src != HDG_MAG || src == HDG_BORE) {
        s_hdg_src = src;
    }
    taskEXIT_CRITICAL(&s_mux);
}

void imu_get_raw(float acc[3], float gyr[3], float mag[3])
{
    taskENTER_CRITICAL(&s_mux);
    memcpy(acc, s_raw_acc, sizeof(s_raw_acc));
    memcpy(gyr, s_raw_gyr, sizeof(s_raw_gyr));
    memcpy(mag, s_raw_mag, sizeof(s_raw_mag));
    taskEXIT_CRITICAL(&s_mux);
}

static void norm3(float *v)
{
    const float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n > 1e-6f) {
        v[0] /= n;
        v[1] /= n;
        v[2] /= n;
    }
}

void imu_cal_capture_level(void)
{
    taskENTER_CRITICAL(&s_mux);
    memcpy(s_cal_up, s_lp_acc, sizeof(s_cal_up));
    taskEXIT_CRITICAL(&s_mux);
    norm3(s_cal_up);
    s_cal_up_valid = true;
    ESP_LOGI(TAG, "cal: up (sensor) = %.3f %.3f %.3f", s_cal_up[0], s_cal_up[1], s_cal_up[2]);
}

bool imu_cal_capture_nose_up(void)
{
    if (!s_cal_up_valid) {
        return false;
    }
    float g2[3];
    taskENTER_CRITICAL(&s_mux);
    memcpy(g2, s_lp_acc, sizeof(g2));
    taskEXIT_CRITICAL(&s_mux);
    norm3(g2);
    const float *u = s_cal_up;
    /* Nose-up pitch makes world-up lean towards body-forward. */
    const float d = g2[0] * u[0] + g2[1] * u[1] + g2[2] * u[2];
    float f[3] = {g2[0] - d * u[0], g2[1] - d * u[1], g2[2] - d * u[2]};
    const float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 0.3f) { /* less than ~17 deg of pitch: not enough to resolve forward */
        return false;
    }
    norm3(f);
    const float r[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
    const float m[9] = {r[0], r[1], r[2], f[0], f[1], f[2], u[0], u[1], u[2]};
    memcpy(g_cfg.mount, m, sizeof(m));
    g_cfg.bore_pitch_deg = 0;
    g_cfg.bore_roll_deg = 0;
    ESP_LOGI(TAG, "cal: mount = [%.2f %.2f %.2f; %.2f %.2f %.2f; %.2f %.2f %.2f]", m[0], m[1], m[2], m[3], m[4],
             m[5], m[6], m[7], m[8]);
    return true;
}

void imu_trim_level(void)
{
    taskENTER_CRITICAL(&s_mux);
    hud_euler_t e;
    hud_quat_to_euler(s_ahrs.q, &e);
    taskEXIT_CRITICAL(&s_mux);
    g_cfg.bore_pitch_deg = e.pitch_deg;
    g_cfg.bore_roll_deg = e.roll_deg;
    ESP_LOGI(TAG, "level trim pitch %.2f roll %.2f", e.pitch_deg, e.roll_deg);
}

static bool calibrate_gyro_bias(float bias[3])
{
    double sum[3] = {0};
    int good = 0;
    for (int i = 0; i < BIAS_SAMPLES; i++) {
        qmi8658_sample_t s;
        if (qmi8658_read(&s) == ESP_OK) {
            const float an = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
            const float gn = sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
            if (fabsf(an - 1.0f) < 0.1f && gn < 0.2f) {
                sum[0] += s.gx;
                sum[1] += s.gy;
                sum[2] += s.gz;
                good++;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000 / IMU_HZ));
    }
    if (good < BIAS_SAMPLES / 2) {
        return false;
    }
    for (int k = 0; k < 3; k++) bias[k] = (float)(sum[k] / good);
    return true;
}

static void imu_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bc = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_IMU_SDA,
        .scl_io_num = BOARD_IMU_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bc, &bus));
    if (qmi8658_init(bus) != ESP_OK) {
        ESP_LOGE(TAG, "IMU missing - attitude frozen at identity");
        vTaskDelete(NULL);
        return;
    }

#if CONFIG_HUD_BACKPACK
    i2c_master_bus_handle_t bus2;
    const i2c_master_bus_config_t bc2 = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = BACKPACK_I2C_SDA,
        .scl_io_num = BACKPACK_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bc2, &bus2) == ESP_OK && mmc5983_init(bus2) == ESP_OK) {
        s_hdg_src = HDG_MAG;
    }
#endif

    float bias[3] = {0};
    ESP_LOGI(TAG, "hold still: estimating gyro bias");
    while (!calibrate_gyro_bias(bias)) {
        ESP_LOGW(TAG, "moving during bias estimation, retrying");
    }
    ESP_LOGI(TAG, "gyro bias %.4f %.4f %.4f rad/s", bias[0], bias[1], bias[2]);

    /* Kp high for fast convergence at start, then settle. */
    hud_ahrs_init(&s_ahrs, 5.0f, 0.02f);
    s_ok = true;

    int64_t last = esp_timer_get_time();
    int64_t start = last;
    TickType_t wake = xTaskGetTickCount();
    int mag_div = 0;
    hud_vec3f_t mag_b = {0};
    bool mag_fresh = false;

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / IMU_HZ));
        qmi8658_sample_t s;
        if (qmi8658_read(&s) != ESP_OK) {
            continue;
        }
        const int64_t now = esp_timer_get_time();
        float dt = (float)(now - last) * 1e-6f;
        last = now;
        if (dt <= 0 || dt > 0.1f) dt = 1.0f / IMU_HZ;

        if (now - start > 3000000 && s_ahrs.kp > 1.0f) {
            s_ahrs.kp = 1.0f;
        }

        const float *m = g_cfg.mount;
        const hud_vec3f_t acc = mount_apply(m, s.ax, s.ay, s.az);
        const hud_vec3f_t gyr = mount_apply(m, s.gx - bias[0], s.gy - bias[1], s.gz - bias[2]);

        /* Reject accel correction under dynamic acceleration or shock (recoil,
         * footfalls): only trust it when |a| is close to 1 g. */
        const float an = sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
        const bool acc_ok = fabsf(an - 1.0f) < 0.15f;

#if CONFIG_HUD_BACKPACK
        if (mmc5983_present() && ++mag_div >= 2) { /* 100 Hz */
            mag_div = 0;
            mmc5983_sample_t ms;
            if (mmc5983_read(&ms) == ESP_OK) {
                mag_b = mount_apply(m, ms.x - g_cfg.mag_offset[0], ms.y - g_cfg.mag_offset[1],
                                    ms.z - g_cfg.mag_offset[2]);
                mag_fresh = true;
                s_raw_mag[0] = ms.x;
                s_raw_mag[1] = ms.y;
                s_raw_mag[2] = ms.z;
            }
        }
#else
        (void)mag_div;
#endif

        taskENTER_CRITICAL(&s_mux);
        hud_ahrs_update(&s_ahrs, gyr, acc_ok ? &acc : NULL, mag_fresh ? &mag_b : NULL, dt);
        s_q_out = hud_quat_mul(s_ahrs.q, trim_quat());
        s_raw_acc[0] = s.ax;
        s_raw_acc[1] = s.ay;
        s_raw_acc[2] = s.az;
        s_raw_gyr[0] = s.gx;
        s_raw_gyr[1] = s.gy;
        s_raw_gyr[2] = s.gz;
        for (int k = 0; k < 3; k++) s_lp_acc[k] += 0.02f * (s_raw_acc[k] - s_lp_acc[k]);
        taskEXIT_CRITICAL(&s_mux);
        mag_fresh = false;
    }
}

void imu_task_start(void)
{
    s_q_out = hud_quat_identity();
    xTaskCreatePinnedToCore(imu_task, "imu", 4096, NULL, 10, NULL, 1);
}
