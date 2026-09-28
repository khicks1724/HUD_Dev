# Firmware (ESP-IDF v5.3+)

```
firmware/
  main/                 app: tasks, config, console, TAK client, live view
  components/hud_core/  portable C (also built on the PC by test_host/)
  components/drivers/   ST7789 (esp_lcd), QMI8658, MMC5983MA
  test_host/            PC tests, preview renderer, CoT end-to-end dump
  partitions.csv        factory 3 MB app + "hudcfg" NVS for settings/certs
  sdkconfig.defaults    ESP32-S3R8: octal PSRAM, 16 MB flash, TLS, SNTP
```

Build and flash:
```
idf.py set-target esp32s3
idf.py menuconfig        # "TAK HUD" menu: defaults for Wi-Fi, TAK, FOV, mirror, backpack, thermal
idf.py build
idf.py -p COMx flash monitor
```

| File | Role |
|---|---|
| `main.c` | Starts everything; display first |
| `app_state.c` | Shared target table (PSRAM), own position with source priority, CoT sink |
| `imu_task.c` | 200 Hz QMI8658 → mount matrix → Mahony; shock rejection; two-pose cal |
| `render_task.c` | 30 Hz ENU/projection/render → ST7789; target nearest to boresight |
| `net_wifi.c` | STA + SNTP (DHCP server first) |
| `tak_client.c` | esp-tls TCP/TLS stream, pings, optional own SA, backoff |
| `mesh_rx.c` | ATAK multicast (XML + protobuf) and a UDP POS/HDG input |
| `fake_targets.c` | Built-in scenario (same as sim_server.py and the web page) |
| `gnss_task.c` | Backpack GNSS (NMEA) → own position + clock |
| `button.c` | BOOT key: mode / thermal / heading align / level trim |
| `thermal.c` | Thermal underlay source (synthetic now, Boson DVP later) |
| `telemetry.c` | `@HUD` JSON over serial, HTTP `/api/state` `/api/frame` |
| `console_cmds.c` | Serial console commands (`help`) |

Pins are in `main/board.h`, taken from Waveshare's schematic.

**Not yet compiled on hardware.** Expect a round of compile fixes on the
first `idf.py build`, mainly around exact ESP-IDF API names in
`lcd_st7789.c`, `telemetry.c` and `console_cmds.c`.
