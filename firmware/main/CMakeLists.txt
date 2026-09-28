idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "hud_config.c"
        "imu_task.c"
        "render_task.c"
        "net_wifi.c"
        "tak_client.c"
        "mesh_rx.c"
        "fake_targets.c"
        "gnss_task.c"
        "button.c"
        "thermal.c"
        "telemetry.c"
        "console_cmds.c"
    INCLUDE_DIRS "."
    REQUIRES hud_core drivers nvs_flash esp_wifi esp_netif esp_event esp-tls esp_http_server
             console driver esp_timer lwip
)
