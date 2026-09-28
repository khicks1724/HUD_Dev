# Roadmap

| # | Stage | Done when | Status |
|---|---|---|---|
| 0 | Math, parsers, renderer on the PC | 339 C tests + 289 JS tests pass; C and JS frames match | **done** |
| 1 | Board bring-up: install ESP-IDF, build, flash | Display draws; horizon stays level when you roll the board | **next** |
| 2 | Fake targets | Rotate the board and the units stay fixed in the world | code done |
| 3 | Prism calibration (mirror, mount, FOV, trim) | CALIB ticks agree with known landmark angles | todo |
| 4 | Own position from ATAK (TAK owner UID, mesh, or USB) | STATUS shows `POS TAK` or `USB` with the phone's coordinates | code done |
| 5 | TAK Server over TLS 8089 | Real server tracks appear; reconnects after Wi-Fi loss | code done |
| 6 | Heading: boresight align → V2 magnetometer | A known unit stays under its icon after a 360° turn | partly (align done) |
| 7 | Standalone: GNSS + LiPo backpack PCB | Works with the phone switched off | design done |
| 8 | Thermal: VPC bench test → S3 DVP (V2T) or P4 UVC (V3) | Hot-only overlay lines up with TAK icons | overlay done, capture todo |
| 9 | ATAK USB plugin | One cable: phone powers the HUD and feeds tracks | HUD side done |
| 10 | V3 board + rail housing + collimated optic | Survives removal and refit with no zero shift | concept |

## Open items in the code

- Magnetometer hard/soft-iron calibration command (`cal mag`) and ellipsoid fit.
- Boson DVP capture back-end (`thermal.c`, `CONFIG_HUD_THERMAL_DVP`).
- Thermal crop offset as a setting; thermal-assisted icon snapping.
- A token for the HTTP live-view API; Wi-Fi provisioning over BLE (the
  `wifi_provisioning` component) instead of the console.
- A shot/shock detector that holds attitude on a rifle mount.
