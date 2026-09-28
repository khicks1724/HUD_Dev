/*
 * qmi8658.h - QST QMI8658A 6-axis IMU over I2C (new i2c_master driver).
 */
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax, ay, az; /* g */
    float gx, gy, gz; /* rad/s */
    float temp_c;
} qmi8658_sample_t;

/* Probes 0x6B then 0x6A on the given bus. Configures +-4 g, +-512 dps,
 * ~235 Hz ODR with the internal low-pass filters enabled. */
esp_err_t qmi8658_init(i2c_master_bus_handle_t bus);

esp_err_t qmi8658_read(qmi8658_sample_t *out);

uint8_t qmi8658_address(void);

#ifdef __cplusplus
}
#endif
