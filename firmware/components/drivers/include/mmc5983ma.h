/*
 * mmc5983ma.h - MEMSIC MMC5983MA 3-axis magnetometer (backpack board).
 *
 * UNTESTED on hardware: written from the datasheet register map. Verify
 * against the datasheet revision you build with.
 */
#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z; /* gauss, sensor frame */
} mmc5983_sample_t;

esp_err_t mmc5983_init(i2c_master_bus_handle_t bus);   /* continuous 100 Hz, auto SET/RESET */
esp_err_t mmc5983_read(mmc5983_sample_t *out);
bool mmc5983_present(void);

#ifdef __cplusplus
}
#endif
