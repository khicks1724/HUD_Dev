#include "qmi8658.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qmi8658";

/* Register map (QMI8658A datasheet rev 1.0) */
#define REG_WHO_AM_I 0x00 /* reads 0x05 */
#define REG_CTRL1 0x02    /* serial interface: bit6 address auto-increment */
#define REG_CTRL2 0x03    /* accel: [6:4] full scale, [3:0] ODR */
#define REG_CTRL3 0x04    /* gyro:  [6:4] full scale, [3:0] ODR */
#define REG_CTRL5 0x06    /* low-pass filters */
#define REG_CTRL7 0x08    /* enable: bit0 accel, bit1 gyro */
#define REG_TEMP_L 0x33
#define REG_AX_L 0x35
#define REG_RESET 0x60
#define WHO_AM_I_VAL 0x05

#define ACC_FS_4G (0x1 << 4)
#define GYR_FS_512DPS (0x5 << 4)
#define ODR_235HZ 0x05 /* 6DOF mode: ~235 Hz */

#define ACC_LSB_PER_G 8192.0f
#define GYR_LSB_PER_DPS 64.0f
#define DEG2RAD 0.017453292519943295f

static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr;

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    const uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, sizeof(b), 50);
}

static esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50);
}

uint8_t qmi8658_address(void)
{
    return s_addr;
}

esp_err_t qmi8658_init(i2c_master_bus_handle_t bus)
{
    const uint8_t candidates[] = {0x6B, 0x6A};
    for (size_t i = 0; i < sizeof(candidates); i++) {
        if (i2c_master_probe(bus, candidates[i], 50) == ESP_OK) {
            s_addr = candidates[i];
            break;
        }
    }
    if (!s_addr) {
        ESP_LOGE(TAG, "not found at 0x6B/0x6A");
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_addr,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &s_dev));

    wr(REG_RESET, 0xB0);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t id = 0;
    ESP_ERROR_CHECK(rd(REG_WHO_AM_I, &id, 1));
    if (id != WHO_AM_I_VAL) {
        ESP_LOGW(TAG, "unexpected WHO_AM_I 0x%02x", id);
    }
    ESP_ERROR_CHECK(wr(REG_CTRL1, 0x40));                        /* auto-increment, little endian */
    ESP_ERROR_CHECK(wr(REG_CTRL2, ACC_FS_4G | ODR_235HZ));
    ESP_ERROR_CHECK(wr(REG_CTRL3, GYR_FS_512DPS | ODR_235HZ));
    ESP_ERROR_CHECK(wr(REG_CTRL5, 0x11));                        /* accel + gyro LPF enabled */
    ESP_ERROR_CHECK(wr(REG_CTRL7, 0x03));                        /* accel + gyro on */
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(TAG, "QMI8658 at 0x%02x, id 0x%02x", s_addr, id);
    return ESP_OK;
}

esp_err_t qmi8658_read(qmi8658_sample_t *o)
{
    uint8_t b[14];
    esp_err_t err = rd(REG_TEMP_L, b, sizeof(b)); /* temp(2) + accel(6) + gyro(6) */
    if (err != ESP_OK) {
        return err;
    }
    const int16_t t = (int16_t)(b[1] << 8 | b[0]);
    const int16_t ax = (int16_t)(b[3] << 8 | b[2]);
    const int16_t ay = (int16_t)(b[5] << 8 | b[4]);
    const int16_t az = (int16_t)(b[7] << 8 | b[6]);
    const int16_t gx = (int16_t)(b[9] << 8 | b[8]);
    const int16_t gy = (int16_t)(b[11] << 8 | b[10]);
    const int16_t gz = (int16_t)(b[13] << 8 | b[12]);
    o->temp_c = t / 256.0f;
    o->ax = ax / ACC_LSB_PER_G;
    o->ay = ay / ACC_LSB_PER_G;
    o->az = az / ACC_LSB_PER_G;
    o->gx = gx / GYR_LSB_PER_DPS * DEG2RAD;
    o->gy = gy / GYR_LSB_PER_DPS * DEG2RAD;
    o->gz = gz / GYR_LSB_PER_DPS * DEG2RAD;
    return ESP_OK;
}
