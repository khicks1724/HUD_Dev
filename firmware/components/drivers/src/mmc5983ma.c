#include "mmc5983ma.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mmc5983";

#define ADDR 0x30
#define REG_XOUT0 0x00 /* X0 X1 Y0 Y1 Z0 Z1 XYZ2 */
#define REG_STATUS 0x08
#define REG_CTRL0 0x09 /* bit0 TM_M, bit3 SET, bit4 RESET, bit5 AUTO_SR_EN */
#define REG_CTRL1 0x0A /* bit7 SW_RST, [1:0] bandwidth */
#define REG_CTRL2 0x0B /* bit3 CMM_EN, [2:0] continuous frequency */
#define REG_PRODUCT_ID 0x2F
#define PRODUCT_ID 0x30

#define NULL_FIELD 131072.0f
#define COUNTS_PER_GAUSS 16384.0f

static i2c_master_dev_handle_t s_dev;
static bool s_present;

static esp_err_t wr(uint8_t reg, uint8_t v)
{
    const uint8_t b[2] = {reg, v};
    return i2c_master_transmit(s_dev, b, 2, 50);
}

bool mmc5983_present(void)
{
    return s_present;
}

esp_err_t mmc5983_init(i2c_master_bus_handle_t bus)
{
    if (i2c_master_probe(bus, ADDR, 50) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dc, &s_dev));
    wr(REG_CTRL1, 0x80); /* software reset */
    vTaskDelay(pdMS_TO_TICKS(15));
    uint8_t reg = REG_PRODUCT_ID, id = 0;
    i2c_master_transmit_receive(s_dev, &reg, 1, &id, 1, 50);
    if (id != PRODUCT_ID) {
        ESP_LOGW(TAG, "product id 0x%02x (expected 0x30)", id);
    }
    wr(REG_CTRL0, 0x08); /* SET pulse to clear offset */
    vTaskDelay(pdMS_TO_TICKS(2));
    wr(REG_CTRL1, 0x00);               /* 100 Hz bandwidth */
    wr(REG_CTRL0, 0x20);               /* auto SET/RESET */
    ESP_ERROR_CHECK(wr(REG_CTRL2, 0x08 | 0x05)); /* continuous, 100 Hz */
    s_present = true;
    ESP_LOGI(TAG, "MMC5983MA ready");
    return ESP_OK;
}

esp_err_t mmc5983_read(mmc5983_sample_t *o)
{
    uint8_t reg = REG_XOUT0, b[7];
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, b, sizeof(b), 50);
    if (err != ESP_OK) return err;
    const uint32_t x = ((uint32_t)b[0] << 10) | ((uint32_t)b[1] << 2) | ((b[6] >> 6) & 0x3);
    const uint32_t y = ((uint32_t)b[2] << 10) | ((uint32_t)b[3] << 2) | ((b[6] >> 4) & 0x3);
    const uint32_t z = ((uint32_t)b[4] << 10) | ((uint32_t)b[5] << 2) | ((b[6] >> 2) & 0x3);
    o->x = ((float)x - NULL_FIELD) / COUNTS_PER_GAUSS;
    o->y = ((float)y - NULL_FIELD) / COUNTS_PER_GAUSS;
    o->z = ((float)z - NULL_FIELD) / COUNTS_PER_GAUSS;
    return ESP_OK;
}
