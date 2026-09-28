# Hardware design study

Three boards, from "buy nothing" to a clean integrated unit. None of these
are fab-ready: they are schematic-level plans with part choices, pin maps,
power budgets and layout rules, meant to be captured in KiCad next.

| Stage | What | Why |
|---|---|---|
| V1 | Waveshare ESP32-S3-LCD-1.3-C alone (+ ATAK phone) | Proves the HUD. Phone supplies own position (TAK, mesh or USB). |
| V2 | **Backpack board** on the Waveshare header ([backpack/](backpack/README.md)) | Adds GNSS, magnetometer, fuel gauge, second button, haptic. Standalone HUD. |
| V2T | **Thermal backpack** variant ([backpack/README.md#thermal-variant-v2t](backpack/README.md#thermal-variant-v2t)) | Adds the FLIR Boson on the same header. Uses every free pin. |
| V3 | **Integrated HUD core board** ([core_board/](core_board/README.md)) | One PCB: MCU, sensors, power, Boson connector, display FPC, rail mount. |

## The constraint that drives everything: pins

The Waveshare board exposes **13 free GPIOs** (GPIO1-5, 7-14) on its
headers. Everything else is already used (LCD 38-42, backlight 20, IMU
45-48, SD 16-18/21, RGB LED 15, VBAT ADC 6, USB-UART 43/44, GPIO19 is wired
to the chip-enable reset circuit).

| Budget | GNSS + mag backpack (V2) | Thermal backpack (V2T) |
|---|---|---|
| Boson 8-bit video + PCLK + VSYNC + HSYNC | - | 11 |
| I2C (mag, fuel gauge, **GNSS over I2C**) | 2 | 2 |
| GNSS UART + PPS | 3 | - (GNSS moves to I2C) |
| Buttons / encoder | 2 | 0 (BOOT key only) |
| Haptic | 1 | 0 |
| **Total of 13** | **8** | **13** |

So V2T fits, but with nothing left over. That's the main reason for V3.

## Power budget (3.7 V LiPo)

| Load | Typical | Peak | Notes |
|---|---|---|---|
| ESP32-S3, Wi-Fi STA, no power save | 110 mA | 350 mA | TX bursts |
| ST7789 + backlight at 80 % | 25 mA | 40 mA | mostly black image |
| QMI8658 + MMC5983MA | 2 mA | | |
| SAM-M10Q GNSS | 8 mA | 20 mA | acquisition |
| **HUD total (V2)** | **~150 mA** | **~400 mA** | 1200 mAh ≈ 7 h |
| FLIR Boson 640 | 150-400 mA @ 3.7 V | 1.55 W | datasheet 500-1550 mW |
| **With Boson (V2T)** | **~400 mA** | **~800 mA** | 2000 mAh ≈ 4-5 h |

The Waveshare board's own 3.3 V LDO (ME6217, 800 mA) and linear charger
(PL4054, a TP4054-class part whose charge current is set by R5; check it
before using a very small cell) are fine for V2. **The Boson must not run
from the Waveshare LDO**: V2T adds its own buck-boost from VBAT.

Files:
- [backpack/README.md](backpack/README.md): V2/V2T schematic plan, pin map, layout rules
- [backpack/bom.csv](backpack/bom.csv): BOM
- [core_board/README.md](core_board/README.md): V3 integrated board
- [mount/README.md](mount/README.md): Picatinny / M-LOK rail mount concept
