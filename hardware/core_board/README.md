# V3: integrated HUD core board

Once V1/V2 prove the software, the clean hardware is one board that carries
everything and bolts into a rail-mount housing ([../mount](../mount/README.md)).
Two MCU options, depending on whether thermal video is in scope.

## Option A: ESP32-S3 (no thermal, or thermal as a low-rate underlay)

| Block | Part | Notes |
|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | Same chip as today, but ~36 usable GPIO instead of 13 |
| IMU | ICM-42688-P or keep QMI8658 | ICM-42688 has lower gyro noise and ±2000 dps / ±16 g |
| Magnetometer | MMC5983MA | As on the backpack |
| GNSS | SAM-M10Q | UART |
| Display | ST7789 240x240 FPC, or a 0.39" micro-OLED | See "Optics" below |
| Power | BQ25185 charger + power path, TPS63802 3.3 V | USB-C PD sink not needed (5 V only) |
| USB | Native USB-C on GPIO19/20 (USB-Serial-JTAG) | Drops the CH343. The phone can talk CDC-ACM directly |
| Boson | DF40 80-pin + AXC translators, DVP | 30 Hz 8-bit, downscaled to the display |

## Option B: ESP32-P4 + ESP32-C6 (recommended if thermal matters)

The ESP32-P4 changes what is possible with the Boson:

- **USB 2.0 High-Speed host**: it can take the Boson's USB VPC video (UVC)
  directly. The S3's USB is Full-Speed only (12 Mbit/s), which can't carry
  640x512 video.
- **MIPI-CSI + ISP and a 2D pixel accelerator (PPA)**: scale, rotate and
  alpha-blend the thermal image under the symbology in hardware.
- **MIPI-DSI**: drives larger or higher-resolution micro-displays.
- It has no radio, so Wi-Fi/BLE comes from an ESP32-C6 over SDIO (the
  esp_hosted pattern Espressif uses on its P4 boards).

The `hud_core` C library ports unchanged. Only the drivers and tasks in
`firmware/main` change.

| Block | Part |
|---|---|
| MCU | ESP32-P4 (32 MB PSRAM variant) |
| Radio | ESP32-C6-MINI-1 (Wi-Fi 6, BLE 5) via SDIO |
| Boson | USB-HS host to the VPC **or** the DF40 80-pin direct (CMOS → DVP) |
| Display | MIPI-DSI micro-OLED (e.g. 0.39"-0.6" 1080p class) + eyepiece |

Dev boards to prototype Option B before a custom PCB: Espressif
ESP32-P4-Function-EV-Board, or Waveshare's ESP32-P4 boards with a C6 on board.

## Optics: the prism is a desk demo, not a sight

The Waveshare prism cube shows a **reflection of the LCD at the LCD's
distance** (a few cm). A real HUD collimates the image so it sits at
infinity, the same focus as the world. Without that:

- your eye can't focus on the symbology and the scene at the same time;
- the icons shift against the world when your eye moves (parallax).

For head or rail use, V3 should use either:
1. a **collimating lens** (focal length = display-to-lens distance) plus a
   beam-splitter combiner, i.e. a reflex-sight or HUD architecture; or
2. a **micro-display + eyepiece** (thermal-monocular style), where the
   thermal image and the symbology are both virtual images and nothing is
   see-through.

Option 2 is far simpler and is how clip-on thermal viewers work. Option 1
keeps real-world see-through, which is where TAK icons earn their keep.
