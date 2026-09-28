# Calibration

Do these once per build, in this order, and `save` after each.

## 1. Display orientation (prism mirror)

The prism reflects the LCD, so the image must be mirrored. The default is
horizontal mirroring (`CONFIG_HUD_MIRROR_X`). If the text reads backwards
or upside down through the cube:
```
mirror 1 0     # try: mirror 0 1, mirror 1 1
```
Waveshare's own prism demos use TFT_eSPI `setRotation(4)`, which is a
mirrored portrait orientation, consistent with this default.

## 2. IMU mount (two poses)

The firmware needs to know how the QMI8658 sits relative to the direction
you look through the prism.
```
cal level      # hold the HUD level, looking through the prism at the horizon, still
cal nose       # pitch the nose up 30-60°, hold still
save
```
The first pose defines "up"; the change between poses defines "forward".
The result is `g_cfg.mount`, a sensor-to-HUD rotation matrix. Check it:
`status` should show pitch ≈ 0 and roll ≈ 0 when level, and pitch goes
positive when you look up.

## 3. Field of view and optical centre (CALIB screen)

`mode 2` shows a crosshair with ticks every 5° (assuming the current FOV).

1. Pick two distant landmarks with a known angle between them, e.g. 20°
   apart measured with a phone compass, or use two telephone poles at a
   known distance.
2. Put one on the centre and read where the other lands on the tick scale.
   If a 20° separation lands on the 15° tick, the real HFOV is 40 × 20/15.
3. `fov <h> <v>` and repeat until the ticks agree. The vertical works the
   same way.
4. If the centre of the view through the cube isn't the crosshair, shift it
   with `bore <dx> <dy>` (pixels).

## 4. Level trim

In CALIB mode, look at a true horizon (sea, a long flat field) and
**long-press** BOOT (or `cal trim`). This zeroes pitch and roll offsets.

## 5. Heading

- **No magnetometer (V1):** put a TAK unit you can actually see in the
  crosshair and **long-press** BOOT. The heading snaps so that unit is on
  the boresight. Re-do it when icons drift. `hdg <deg>` sets it by hand.
- **Magnetometer (V2):** do a figure-8 with the HUD in its final enclosure
  (on the rifle, if it's rail-mounted). Hard-iron offsets go in
  `g_cfg.mag_offset`. The console command for automatic min/max capture is
  a TODO; for now read `imu` and compute (max+min)/2 per axis.

## 6. Thermal boresight (V2T/V3)

With the Boson in HOT mode, look at a warm point source (a person, a
kettle) at more than 20 m that also carries a TAK marker, or at the centre
of the crosshair. Shift the crop so the heat spot sits under the
crosshair. The crop offset lives in `thermal.c` (`src_x/src_y`) and should
become a setting.
