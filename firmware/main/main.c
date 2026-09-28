/*
 * TAK HUD for the Waveshare ESP32-S3-LCD-1.3-C (prism).
 *
 * Core 1: imu (200 Hz) -> render (30 Hz) -> ST7789
 * Core 0: Wi-Fi, TAK client, mesh/UDP inputs, fake targets, GNSS, console,
 *         live-view HTTP API
 *
 * Bring-up order follows docs/ROADMAP.md: with no configuration at all the
 * HUD boots into fake-target mode at the Kconfig position so the display,
 * IMU and projection can be checked by just rotating the device.
 */
#include "app_state.h"
#include "button.h"
#include "console_cmds.h"
#include "esp_log.h"
#include "fake_targets.h"
#include "gnss_task.h"
#include "hub_link.h"
#include "hud_config.h"
#include "imu_task.h"
#include "mesh_rx.h"
#include "net_wifi.h"
#include "nvs_flash.h"
#include "render_task.h"
#include "tak_client.h"
#include "telemetry.h"
#include "thermal.h"

static const char *TAG = "main";

void app_main(void)
{
    /* Default NVS holds Wi-Fi driver calibration; hudcfg holds our settings. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    hud_config_load();
    app_state_init();

    render_task_start(); /* display first so the user sees something */
    imu_task_start();
    thermal_start();
    fake_targets_start();
    gnss_task_start();
    hub_link_start();
    button_start();

    net_wifi_start();
    tak_client_start();
    mesh_rx_start();
    telemetry_http_start();

    ESP_LOGI(TAG, "TAK HUD running. Serial console: type 'help'.");
    console_start();
}
