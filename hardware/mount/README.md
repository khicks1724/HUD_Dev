# Rail mount concept (Picatinny / M-LOK)

Goal: carry the HUD electronics, battery and FLIR Boson as one unit on a
rifle rail, as a **situational-awareness display**: TAK units plus a
toggleable thermal view. It is not an aiming device, and this design makes
no attempt at ballistic or fire-control functions.

## One interface: Picatinny

Design the housing with a **MIL-STD-1913 Picatinny clamp** and treat M-LOK
as "add a short M-LOK Picatinny section". Every M-LOK handguard takes a 3-
to 5-slot M-LOK rail section, and one clamp design then fits both, plus
top rails, helmets with rail adapters, and bench fixtures.

| Picatinny (MIL-STD-1913) | Value |
|---|---|
| Rail top width | 21.2 mm (0.835") |
| Slot (recoil groove) width | 5.23 mm (0.206") |
| Slot pitch | 10.01 mm (0.394") |
| Dovetail angle | 45° flanks |

| M-LOK (Magpul) | Value |
|---|---|
| Slot | 32 mm × 7 mm |
| Slot pitch | 40 mm (8 mm web between slots) |
| Fastening | T-nut, #10-24 or M5 screw, 90° turn |

Clamp options, in order of preference:
1. **QD lever clamp** (ADM/LaRue-style): repeatable return-to-zero when
   you remove and refit it. That matters because the HUD's heading and the
   thermal boresight are calibrated relative to the rail.
2. Cross-bolt with a 5 mm recoil lug that sits in a slot: cheapest, and
   very repeatable if you always push it forward in the same slot.

## Layout (side view, muzzle to the left)

```
   muzzle ◄──────────────────────────────────────────────── user
         ┌──────────┬──────────────────────┬──────────────┐
         │  BOSON   │  ESP32 / P4 board    │  DISPLAY +   │
         │  50° 640 │  IMU (rigid), MAG    │  EYEPIECE or │
         │  lens    │  (top, away from     │  combiner    │
         │  ──►     │   barrel steel)      │  ◄── eye     │
         ├──────────┴──────────────────────┴──────────────┤
         │  LiPo 2000 mAh (low CG)    USB-C (charge/data) │
         └───────────────[ Picatinny QD clamp ]───────────┘
                        ═══════ rail ═══════
```

## Things that behave differently on a rifle

- **Recoil shock.** Rifle rail shock can reach hundreds of g for
  sub-millisecond pulses. The Boson is rated 1500 g @ 0.4 ms. The QMI8658
  (±16 g max) will saturate every shot. The firmware already refuses
  accelerometer corrections when |a| isn't close to 1 g, but gyro
  integration during a clipped pulse still causes a small attitude jump:
  1. use a ±2000 dps gyro range on the rail build (ICM-42688-P in V3);
  2. optionally, detect the shot spike and hold attitude for ~50 ms.
- **Keep the optical path rigid and isolate the rest.** Boson, IMU and
  display/combiner must share one rigid chassis bolted to the clamp. The
  PCB can float on grommets only if the IMU sits on the rigid chassis.
  Pot or stake connectors and the battery.
- **The magnetometer and the steel barrel/rail.** Hard- and soft-iron
  distortion changes with where the unit sits on the gun. Calibrate the
  magnetometer on the weapon (figure-8 with the rifle), and keep the sensor
  as high and as far rearward as the housing allows. Expect 2-5° until it's
  calibrated. Boresight alignment to a known TAK unit (long-press) is a good
  field fallback.
- **Field of view.** The Boson 50° lens is wider than a small display
  shows comfortably. The firmware crops the camera image to the display
  FOV, so thermal and TAK icons share one angular scale (docs/THERMAL.md).
- **Eye relief.** The Waveshare prism shows an uncollimated image a few cm
  away, so it can't be used at rail eye relief. Use a micro-display +
  eyepiece or a collimated combiner (hardware/core_board/README.md).

## Files

- [hud_rail_mount.scad](hud_rail_mount.scad): parametric OpenSCAD concept
  of the housing with a Picatinny clamp, Boson bay, electronics bay,
  battery bay and eyepiece tube. Open it in OpenSCAD (free) and export STL
  for a first 3D print.
