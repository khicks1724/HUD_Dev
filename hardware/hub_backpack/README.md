# V2H: USB-C hub backpack (recommended)

The backpack becomes the system's **power and I/O hub**. It carries the
battery, powers the FLIR Boson and takes its video, talks to the ATAK phone
over USB-C, and feeds the HUD head (the Waveshare ESP32-S3 board) with
power, tracks and a ready-to-draw thermal image.

> Status: design study (block level, part choices, pin map, link protocol).
> The HUD-side link firmware is written (`firmware/main/hub_link.c`); the
> hub's own ESP32-P4 firmware is outlined below but not written yet.

## Why the backpack needs its own processor

Everything hinges on **who can be the USB host for the camera**.

| Fact | Consequence |
|---|---|
| The Boson streams video only as a **USB 2.0 High-Speed UVC device** (or 1.8 V parallel CMOS). 640×512 at 30-60 Hz is 80-160 Mbit/s. | The camera needs a High-Speed USB **host**. |
| The ESP32-S3's USB is Full-Speed (12 Mbit/s), and on the Waveshare board it isn't even exposed: the USB-C goes to a CH343 serial chip, and GPIO19/20 are used for reset and backlight. | The HUD head can never host the camera. |
| An Android phone *can* host it, but only while the phone is plugged in and running a plugin. | Thermal would die whenever the phone is unplugged or ATAK is closed. |
| The **ESP32-P4** has a USB 2.0 HS OTG port with a built-in PHY, a second Full-Speed OTG port, a 2D pixel accelerator (scale/crop/blend) and a JPEG encoder. ESP-IDF's `usb_host_uvc` supports YUY2/MJPEG on it. | One chip can host the camera, act as a USB device to the phone, and preprocess the video. |

So the recommended hub puts an **ESP32-P4 on the backpack**. The Waveshare
S3 stays the HUD head: it keeps the display, the IMU, Wi-Fi/TAK, and the
renderer.

## Block diagram

```
      USB-C "CAM"  (host, 5 V out)                 USB-C "PHONE" (device)
   Boson + VPC ────────────┐                  ┌──────────────── ATAK phone
   (later: Boson direct    │ USB2 HS          │ USB FS (CDC-ACM; optional UVC preview)
    on an 80-pin DF40)     ▼                  ▼
                     ┌─────────────────────────────────┐
                     │  ESP32-P4  (hub processor)      │
                     │  - UVC host: Y16/YUY2 640×512   │
                     │  - PPA: crop to HUD FOV, scale  │
                     │    to 240×240, 8-bit            │
                     │  - phone link: tracks/own pos   │
                     │  - GNSS (UART), fuel gauge (I2C)│
                     └───────┬───────────────┬─────────┘
                SPI 20 MHz   │               │ UART 2 Mbit/s
             (thermal frames)│               │ (trk / fix / hdg lines)
                             ▼               ▼
            ═══════════ Waveshare GPIO header ═══════════
            │   ESP32-S3 HUD head: IMU, Wi-Fi/TAK,       │
            │   renderer, prism. Magnetometer on its I2C │
            ══════════════════════════════════════════════
                             ▲ 5 V (USB VCC pin, via ideal diode)
      ┌──────────────────────┴───────────────────────────────┐
      │ POWER: USB-C "PWR IN" (5 V/3 A) → BQ25895 charger/   │
      │ power path → 1S Li-ion 2×18650 (6-7 Ah) → VSYS       │
      │   ├─ TPS61088 boost → 5 V/3 A: CAM port (1.5 A limit), HUD │
      │   └─ TPS63802 → 3.3 V: P4, GNSS, logic               │
      └──────────────────────────────────────────────────────┘
```

## Ports and USB roles

| Port | Role | Power | Notes |
|---|---|---|---|
| **CAM** (USB-C) | P4 **host** (DFP), HS | **Sources** 5 V through a TPS25200 current-limited switch (1.5 A), Rp = 22 kΩ | The Boson VPC plugs in here. The VPC makes the Boson's 3.3 V from our 5 V (Boson draws 560 mA typ, ~1 A max at 3.3 V). |
| **PHONE** (USB-C) | P4 **device** (UFP), FS | Rd = 5.1 kΩ. The phone supplies VBUS, which we only sense (self-powered device, so the phone isn't drained) | Enumerates as CDC-ACM. An ATAK plugin writes tracks and its own position and reads status. Optional: a second UVC function so ATAK can show a thermal preview (MJPEG from the P4's encoder fits in FS). |
| **PWR IN** (USB-C) | Sink only | 5 V/3 A charge input (Rd) | Charges the pack. Everything keeps running while charging (power path). |
| Header to HUD | UART + SPI + I2C | 5 V into the Waveshare **USB VCC** pin through an LM66100 ideal diode | The diode stops back-feed when the Waveshare's own USB-C is also plugged in. Remove the Waveshare's own LiPo. |

**Charging the phone from the backpack** (optional, rev B): the PHONE port
would need a USB-PD controller (TI TPS65987D or an Infineon EZ-PD CCG3PA)
to swap power roles, so the phone stays USB host but becomes the power
sink, as USB-C docks do. It's worth it for long missions, but it adds a
configured PD chip, so rev A leaves it out.

## Link: hub → HUD head (implemented on the HUD side)

| Signal | S3 GPIO | Direction | Use |
|---|---|---|---|
| UART1 RX | 12 | hub → HUD | text lines at 2 Mbit/s, same format as the USB console |
| UART1 TX | 13 | HUD → hub | status, attitude (for the phone), acks |
| SPI3 CLK | 10 | hub → HUD | thermal frames, S3 is the SPI **slave** |
| SPI3 MOSI | 11 | hub → HUD | |
| SPI3 CS | 9 | hub → HUD | |
| READY | 14 | HUD → hub | high when the S3 has a receive buffer queued (handshake) |
| I2C SDA / SCL | 1 / 2 | HUD | magnetometer (MMC5983MA) on the backpack, close to the header and far from the boost converter |
| BTN / HAPTIC | 3 / 4 | HUD | encoder or button, vibration motor |
| spare | 5, 7, 8 | | |

**UART lines** (`\n`-terminated ASCII, easy to sniff):
```
fix <lat> <lon> <hae>                                   own position (phone GPS or hub GNSS)
trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign> one track
hdg <deg> <gain>                                        heading nudge (phone compass)
thm <0|1|2>                                             thermal mode off/full/hot
```

**SPI thermal frames:** each 240×240 8-bit frame is sent as 4 chunks of
60 rows. Each chunk is a 16-byte header followed by 14 400 bytes:
```
0  u32  magic 'THM1' (0x314D4854 little-endian)
4  u16  frame id
6  u16  first row (0, 60, 120, 180)
8  u16  rows in chunk (60)
10 u16  width (240)
12 u8   flags (bit0: already cropped to the HUD FOV)
13 u8   hot threshold suggestion
14 u16  reserved
```
That's 57.6 kB per frame, about 13.8 Mbit/s at 30 fps, comfortable at a
20 MHz SPI clock. The hub does the crop and scale (P4 PPA), so the S3 only
copies rows into a PSRAM frame and draws.

## Part choices

| Block | Part | Why |
|---|---|---|
| Hub MCU | **ESP32-P4** (in-package 32 MB PSRAM variant), or a castellated P4 core module on rev A | USB-HS host + FS device, PPA, JPEG. A module avoids the high-speed layout on the first spin. |
| Charger | **TI BQ25895** | 1S, 3 A charge, power path, I2C, input current limit from Type-C CC |
| 5 V boost | **TI TPS61088** | 3 A+ from 1S. Covers the camera (≈1.5 W), HUD (≈1 W) and headroom. |
| 3.3 V | **TI TPS63802** | Buck-boost, stays in regulation down to an empty cell |
| CAM VBUS switch | **TI TPS25200** (or TPS2553) | Current limit + fault flag to the P4 |
| Back-feed protection | **TI LM66100** ideal diode | 5 V to the Waveshare USB VCC pin |
| USB ESD | **TI TPD2EUSB30** / TPD4E05U06 | on all three connectors |
| Fuel gauge | **MAX17048** (P4 I2C) | |
| GNSS | **u-blox SAM-M10Q** (P4 UART) | Own position with the phone unplugged, plus UTC for TLS |
| Magnetometer | **MMC5983MA** (S3 I2C) | Heading. Keep it away from the boost inductor and the cells. |
| Battery | 1S2P 18650 Li-ion with protection board, 6-7 Ah | Or a 3-4 Ah LiPo pouch for a slimmer build |
| Connectors | 3 × USB-C 16-pin (mid-mount), 2.54 mm headers to the Waveshare, JST-PH 2-pin battery | |

## Power budget

| Load | Typical | Peak |
|---|---|---|
| Boson 640 via VPC (5 V side) | 1.0-1.6 W (datasheet 0.5-1.55 W) | ~3.5 W at start-up (size supply for 1 A at 3.3 V) |
| ESP32-P4 (UVC + PPA active) | 0.6 W | 1 W |
| HUD head (S3, Wi-Fi, LCD) | 0.6 W | 1.3 W |
| GNSS, magnetometer, gauge | 0.05 W | 0.1 W |
| Conversion losses (~88 %) | 0.3 W | |
| **Total** | **≈3 W** | **≈6 W** |

A 6.8 Ah 1S pack (25 Wh) gives about **7-8 h** with thermal on, and more
with thermal off (the P4 can switch the CAM port off through the TPS25200).

## HUD-side behaviour (firmware)

`menuconfig → TAK HUD → Hub backpack fitted` (`CONFIG_HUD_HUB`):
- `hub_link.c` reads UART1 lines and feeds the same track/own-position
  paths as the USB console and the TAK client. Own-position priority puts
  the hub's `fix` at the USB level, below the HUD's own GNSS.
- Thermal source "Hub (SPI)" receives frames with a DMA SPI slave and hands
  them to the renderer. The double-press on BOOT still cycles Off/Full/Hot,
  and `thm` from the phone does the same.
- The HUD's own Wi-Fi/TAK keeps working: tracks from both paths merge in
  one table (same UID = same entry).

## Hub firmware (ESP32-P4), to write

1. `usb_host_uvc`: open the Boson, pick 640×512 Y8/YUY2 (AGC'd), 30 fps.
2. PPA: crop the centre to the HUD FOV (`thermalCrop`: ≈499×499 for 40° in
   a 50° lens), scale to 240×240, take luma.
3. SPI master: wait for READY, then send 4 chunks per frame.
4. TinyUSB device on the FS port: CDC-ACM for the phone (lines in the same
   format; forwarded to the UART), plus an optional UVC function carrying
   JPEG-encoded thermal for an ATAK viewer.
5. GNSS NMEA (reuse `hud_nmea.c`), fuel gauge, CAM power switch, and a
   status line to the HUD (`bat <pct> cam <state>`).

## Bring-up without a custom PCB

1. Waveshare ESP32-P4 dev board (or Espressif ESP32-P4-Function-EV-Board)
   + Boson + VPC in its USB-A/C host port: prove UVC capture and cropping.
2. Jumper the P4's SPI/UART to the Waveshare header pins in the table
   above, and flash the HUD with `CONFIG_HUD_HUB`.
3. Phone: a USB-C OTG adapter to the P4's FS port. Test with a serial
   terminal app sending `trk`/`fix` lines, then build the ATAK plugin.
4. Power from a USB-C power bank until the PCB exists.

## Alternatives considered

| Option | Verdict |
|---|---|
| **Phone as the brain**: a plain USB hub; the phone hosts the Boson and forwards thermal to the HUD over serial | Least hardware, but thermal only works with the phone attached and running a plugin, and it costs phone battery and adds latency. Good for a quick demo with an off-the-shelf USB-C hub with PD pass-through. |
| **S3 captures the Boson parallel port** (V2T) | Needs 1.8→3.3 V translators and the 0.4 mm connector, uses all 13 header pins, and leaves no USB-C for the phone. Superseded. |
| **Single integrated P4 + C6 board** (V3) | The end state: drop the Waveshare board, and the P4 drives a collimated micro-display directly. V2H's P4 firmware carries over unchanged. |
