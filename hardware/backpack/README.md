# Backpack board (V2 / V2T)

A small PCB that plugs onto the Waveshare ESP32-S3-LCD-1.3 GPIO headers
and turns the V1 prototype into a standalone HUD: its own position, its own
absolute heading, battery gauge, and a real button, with an optional
thermal-camera variant.

> Status: design study, not fab-ready. Before layout, re-check the header
> pin order against Waveshare's schematic (`ESP32S3_1.3inch.pdf`, nets H1/H2),
> since the header silk and the schematic numbering have differed between
> board revisions.

## Block diagram (V2)

```
             Waveshare header (3V3, GND, VBAT sense, GPIO)
   ┌──────────────────────────────────────────────────────────────┐
   │  GPIO10 SDA ───┬──────────────┬───────────────┐              │
   │  GPIO11 SCL ───┼──────────────┼───────────────┤              │
   │                │              │               │              │
   │         ┌──────┴─────┐  ┌─────┴──────┐  ┌─────┴──────┐       │
   │         │ MMC5983MA  │  │ MAX17048   │  │ (spare I2C │       │
   │         │ magnetometer│ │ fuel gauge │  │  e.g. BMP) │       │
   │         └─────┬──────┘  └────────────┘  └────────────┘       │
   │  GPIO9  ◄─────┘ DRDY                                         │
   │                                                              │
   │  GPIO12 RX ◄──── TXD ┌────────────┐                          │
   │  GPIO13 TX ────► RXD │ SAM-M10Q   │ (patch antenna on top,   │
   │  GPIO14    ◄──── PPS │ GNSS       │  keep-out ring, sky view)│
   │                      └────────────┘                          │
   │  GPIO7/8  ◄──── button / encoder A,B                         │
   │  GPIO5    ────► AO3400 ──► ERM haptic motor                  │
   └──────────────────────────────────────────────────────────────┘
```

## Part choices

| Function | Part | Why this one | Alternatives |
|---|---|---|---|
| GNSS | **u-blox SAM-M10Q** | Antenna built in (15.5 mm patch), so there's no RF layout to get wrong. Concurrent GPS/GLONASS/Galileo/BeiDou. UART **and** I2C, ~8 mA tracking. | MAX-M10S + 18 mm patch (smaller, needs RF design); Quectel L76K (cheaper) |
| Magnetometer | **MEMSIC MMC5983MA** | 18-bit, ~0.4 mG noise, internal SET/RESET removes its own offset drift. Driver already in `firmware/components/drivers`. | ST LIS3MDL, Bosch BMM350 |
| Heading alternative | CEVA/Bosch **BNO085** | 9-axis with on-chip fusion. Least firmware, but it duplicates the QMI8658. | |
| Fuel gauge | **MAX17048** | ModelGauge, no sense resistor, I2C 0x36. | Use the board's VBAT ADC (GPIO6) only |
| Button | 6 mm tactile, or Alps EC11 encoder | Range, declutter and brightness without menus. | |
| Haptic | 10 mm ERM + AO3400 + BAT54 flyback | Silent alerts, e.g. "new hostile within 500 m". | Piezo buzzer |
| Connectors | 2 × 2.54 mm female headers | Plug onto the Waveshare pins | |

**I2C map (bus 2, GPIO10/11, 400 kHz):** MMC5983MA 0x30, MAX17048 0x36,
SAM-M10Q DDC 0x42 (optional, used in V2T). The on-board QMI8658 stays on
its own bus (GPIO47/48).

## Pin map (V2)

| ESP32 GPIO | Backpack net | Firmware constant (`firmware/main/board.h`) |
|---|---|---|
| 10 | I2C2_SDA | `BACKPACK_I2C_SDA` |
| 11 | I2C2_SCL | `BACKPACK_I2C_SCL` |
| 9 | MAG_DRDY | `BACKPACK_MAG_DRDY` |
| 12 | GNSS_TXD → ESP RX | `BACKPACK_GNSS_RX` |
| 13 | ESP TX → GNSS_RXD | `BACKPACK_GNSS_TX` |
| 14 | GNSS_PPS | `BACKPACK_GNSS_PPS` |
| 7 / 8 | BTN_A / BTN_B (encoder) | `BACKPACK_BTN_A/B` |
| 5 | HAPTIC_EN | `BACKPACK_HAPTIC` |
| 1-4 | spare | |

Enable in firmware with `idf.py menuconfig` → TAK HUD → *Backpack board fitted*.

## Layout rules that matter for a HUD

1. **Magnetometer placement dominates heading accuracy.** Put the
   MMC5983MA at the far edge from the LiPo, the haptic motor, the display
   backlight return and any steel screws. Stay 15 mm or more from anything
   switching more than 50 mA. No copper pour directly under the sensor; use
   a keep-out on all layers.
2. Route the haptic motor current in a tight loop on its own ground return,
   away from the magnetometer.
3. **GNSS antenna** (SAM-M10Q patch): face up (sky) in the worn
   orientation, with nothing metal above it. Put a solid ground plane under
   the module per the integration manual, and keep the display/prism from
   shadowing it.
4. Align the magnetometer axes to the QMI8658 axes (or record the rotation).
   The firmware's `mount` matrix assumes the magnetometer shares the IMU
   frame. Otherwise add a second matrix.
5. Add a test pad on every I2C line and PPS, and a 0 Ω jumper to cut the
   haptic.
6. Four M2 holes line up with the case, and the board is 1.0 mm thick to
   save height.

## Heading accuracy you can expect

| Source | Typical error | Notes |
|---|---|---|
| Gyro only (V1) | drifts 0.5-5°/min | Fix by long-pressing on a known unit (boresight align) |
| Phone compass via UDP/USB | 5-15° | Only if the phone faces the same way as the HUD |
| MMC5983MA, hard-iron calibrated | 1-3° | After figure-8 calibration in the final enclosure |
| + soft-iron calibration | ~1° | Ellipsoid fit (TODO in firmware) |
| Dual-antenna GNSS heading | 0.2-0.5° | Needs ≥ 0.5 m baseline: vehicle-mounted only |

On a 1 km target, 2° of heading error puts the icon about 35 m off. That's
fine for "they're over there", not for exact overlay. See docs/HUD_MATH.md.

---

## Thermal variant (V2T)

Adds the FLIR Boson 640 (your 21640AS50, 50° HFOV) on the same header. Full
rationale is in [docs/THERMAL.md](../../docs/THERMAL.md). The electrical
differences:

- **Video:** the Boson CMOS port (8-bit mono tap, PCLK 13.5 MHz in 30 Hz
  averager mode) → **SN74AXC8T245** (D0-D7) + **SN74AXC4T245** (PCLK,
  VSYNC, HSYNC/DE) level shift from 1.8 V to 3.3 V → ESP32-S3 LCD_CAM (DVP).
  The Boson I/O is 1.8 V and the ESP32-S3 needs ~2.5 V for a logic high,
  so translators are mandatory.
- **GNSS moves to I2C** (SAM-M10Q DDC at 0x42) to free the UART pins.
  There are no buttons (BOOT key only) and no haptic.
- **Power:** TPS63802 buck-boost from VBAT → 3.3 V / 2 A for the Boson, with
  a load switch (TPS22918) so the firmware can turn the camera off. Add a
  TLV74018 1.8 V LDO for the translators' A-side.
- **Boson connector:** Hirose **DF40HC(4.0)-80DS-0.4V(51)** mates the
  Boson's DF40C-80DP (0.4 mm pitch, 4.0 mm stack). Hand-soldering 0.4 mm is
  hard: use a stencil and hot air, or have the fab assemble it. For the
  bench, use the FLIR USB VPC (421-0061-00) instead and a PC or Pi.
- **Boson control UART** (921600 baud, 1.8 V) isn't wired: there are no
  pins left. Configure the camera once over USB with the VPC and FLIR's
  Boson app (8-bit CMOS output, averager on, save to flash). It boots into
  that configuration afterwards.

| ESP32 GPIO | V2T net |
|---|---|
| 1, 2, 3, 4, 5, 7, 8, 9 | CAM_D0-D7 (via AXC8T245) |
| 10 | CAM_PCLK |
| 11 | CAM_VSYNC |
| 12 | CAM_HSYNC (DE) |
| 13 | I2C2_SDA (mag, fuel gauge, GNSS DDC) |
| 14 | I2C2_SCL |

If you build V2T, update `board.h` accordingly. The V2 and V2T pin maps are
not compatible.
