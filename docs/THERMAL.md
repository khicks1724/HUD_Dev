# FLIR Boson thermal overlay

## Your camera

The label **21640AS50-6IAER** decodes as a **Boson 640 × 512, 50° HFOV
short lens, shutterless, 60 Hz** core (distributor listings for
"21640AS50"). With 12 µm pixels the vertical FOV is about 41°.

Datasheet facts that drive the design (Boson product datasheet, rev 340):

| | |
|---|---|
| Supply | 3.3 V, 500-1550 mW depending on configuration and temperature |
| Video out | CMOS parallel (8/16-bit, **1.8 V** logic), BT.656-like, or USB |
| Pixel clock | 27 MHz, or 13.5 MHz with the frame averager (30 Hz) |
| Control | UART 921600 8-N-1 (1.8 V) or USB virtual serial |
| Connector | Hirose DF40C-80DP-0.4V(51), mate DF40HC(4.0)-80DS-0.4V(51) |
| Shock | 1500 g @ 0.4 ms |
| Size / mass | 21 × 21 × 11 mm engine, from 7.5 g (plus lens) |

## Interface board (VPC)

**Buy the FLIR Boson USB VPC Kit, 421-0061-00** (~$193 at GroupGets,
about 6 weeks lead time). It converts the 80-pin connector to USB-C: the
camera shows up as a normal UVC webcam, plus a virtual serial port for the
Boson SDK/app. There's also a USB + analog variant (421-0062-00) if you want
composite video for a cheap monitor. You need the VPC regardless:

- to configure the camera (palette, AGC, averager, **CMOS output mode**)
  with FLIR's Boson application, and save to its flash;
- as the fastest bench path: Boson → VPC → laptop or Raspberry Pi.

## Can the ESP32-S3 show thermal? Three ways, compared

| Path | Works on the S3? | Frame rate on the HUD | Effort |
|---|---|---|---|
| **VPC USB (UVC)** → ESP32-S3 | **No.** The S3's USB is Full-Speed (12 Mbit/s); 640×512 8-bit at 30 Hz is ~79 Mbit/s | - | - |
| **Boson CMOS 8-bit → level shifters → S3 DVP (LCD_CAM)** | Yes (V2T backpack) | 15-30 Hz, cropped and downscaled to 240×240 | Custom flex/backpack PCB, 0.4 mm connector |
| **VPC USB → ESP32-P4 (USB-HS host, UVC)** | Needs the P4 (V3 option B) | 30-60 Hz, hardware scaling/blend | New MCU board; P4's `usb_host_uvc` supports YUY2/MJPEG |
| VPC USB → Raspberry Pi/laptop (companion) | Doesn't use the S3 for video | 60 Hz | Easiest for experiments |

**Recommendation (updated):** build the **V2H USB-C hub backpack**
([hardware/hub_backpack](../hardware/hub_backpack/README.md)). An ESP32-P4
on the backpack powers the Boson and hosts its USB video (via the VPC on a
USB-C port, later directly on the 80-pin connector), crops and scales it to
the HUD's field of view, and streams 240×240 frames to the HUD over SPI.
The ATAK phone plugs into the same backpack. The Boson also exposes native
USB2 (UVC) on its 80-pin connector, so a later revision can drop the VPC.

Earlier steps still apply:
1. **Now:** order the VPC and try it on a laptop to verify the camera and
   the 50° FOV, and set it to 8-bit CMOS output with the averager on.
2. **HUD prototype:** the firmware already has the overlay pipeline (below).
   Drive it with the synthetic source (`menuconfig → Thermal underlay
   source → Synthetic`) to tune how it looks through the prism.
3. **Real thermal on the HUD:** either build the V2T backpack
   (hardware/backpack) for the S3, or move to the ESP32-P4 (V3 option B)
   and plug the VPC in over USB. For a rail-mounted thermal + TAK unit I'd
   go with **P4 + VPC**: no 0.4 mm connector work, full frame rate, and the
   same `hud_core` code.

## How the overlay works (implemented)

`hud_render()` takes an optional 8-bit frame and a mode, and the BOOT key
**double-press** (or console `thermal`, or `/api/thermal`) cycles it:

| Mode | What you see through the prism |
|---|---|
| OFF | Symbology only |
| FULL | The whole thermal image in white-hot greyscale, dimmed to 75 %, under the white symbology |
| HOT | Only pixels above a threshold (e.g. people, engines). Everything else stays black, i.e. **see-through** in the prism |

HOT mode is the useful one on a beam-splitter HUD: black pixels emit no
light, so you still see the real world, with hot things outlined on top of
it and TAK icons on the same angular scale.

**Matching the FOV.** The camera sees 50°; the prism shows about 40°
(measure yours with the CALIB screen). The renderer crops the central part
of the camera image that corresponds to the HUD FOV, so a hot spot and a TAK
icon for the same person line up (`thermal.c: compute_crop`):

```
fx_cam  = (640/2) / tan(50°/2)             = 686 px
crop_w  = 2 * fx_cam * tan(HUD_hfov/2)     = 499 px   (for a 40° HUD)
```

This assumes the camera is boresighted with the HUD, meaning mounted
parallel in the same housing. Residual offset is a fixed pixel shift, which
you can calibrate the same way as the HUD boresight.

**Thermal-assisted registration (future).** With both in one frame, a TAK
icon can snap to the nearest hot blob within a degree or two. That removes
most of the GNSS/compass error for people and vehicles that are warm. It's
the cheapest route to "the icon is on the person".

## Power

Budget 1.0-1.5 W for the Boson. On one 2000 mAh LiPo, the HUD alone runs
about 12 h, and with the Boson about 4-5 h. Give the camera its own 3.3 V
buck-boost with a load switch so thermal can be switched off to save
power.
