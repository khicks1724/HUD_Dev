# HUD math

One convention everywhere: C (`hud_core`), JavaScript (`web/hud_math.js`)
and Python (`tools/hudmath.py`). They're cross-checked by
`tests/vectors.json`.

## Frames

| Frame | Axes |
|---|---|
| LLA | WGS-84 latitude, longitude (deg), height above ellipsoid (m). CoT `hae` is HAE; NMEA gives MSL + geoid separation, and `hud_nmea` adds them |
| ECEF | Earth-centred, Earth-fixed (m) |
| ENU | Local tangent plane at the observer: x East, y North, z Up |
| Body (HUD) | x right, y forward (boresight through the prism), z up |
| Screen | 240×240 px, origin top-left, optical centre `(cx, cy)` |

The attitude quaternion `q` maps body to world. With the identity
quaternion, the HUD is level and looking due north.

Euler angles (for UI only): heading ψ (clockwise from true north), pitch θ
(nose up +), roll φ (right side down +), with `R = Rz(-ψ) · Rx(θ) · Ry(φ)`.

## Pipeline, per target, every frame (30 Hz)

```
target LLA ─┐
            ├─► ECEF difference ─► rotate to ENU at observer        (hud_lla_to_enu)
own LLA ────┘
ENU ─► v_body = q⁻¹ · v_enu                                           (hud_quat_rotate_inv)
v_body = (right, fwd, up)
fwd ≤ 0         → behind: edge cue
sx = cx + fx · right / fwd,  sy = cy − fy · up / fwd                 (hud_project)
fx = (W/2) / tan(HFOV/2),    fy = (H/2) / tan(VFOV/2)
outside 0..239  → edge cue: ray from centre, clamped to an inset rectangle
```

The ENU step uses the exact ECEF rotation in double precision; the rest is
float. At 5 km a flat-earth approximation would already be off by about 2 m
vertically (Earth curvature), so we don't use one.

## Attitude (Mahony AHRS, 200 Hz)

- The gyro integrates `q̇ = ½ q ⊗ (0, ω)`.
- The accelerometer corrects roll/pitch via `e = a × (q⁻¹ · up)`, but **only
  when |a| ≈ 1 g** (±0.15 g). Under footfalls or recoil it's ignored.
- The magnetometer (V2) corrects yaw. The field is rotated to world, its
  horizontal part is flattened onto north, and the error is `e = m × w`.
- With no magnetometer, yaw is gyro-only and drifts slowly. The
  `imu_set_heading()` nudges from phone, boresight or console rotate the
  whole attitude about world-up.

## Error budget: how far off an icon lands

| Error source | Size | Effect |
|---|---|---|
| Heading | 1° | 17 m at 1 km |
| Heading | 3° | 52 m at 1 km; ~6 px per degree on a 40° / 240 px display |
| Own GPS | 5 m | 5 m at any range: up to ~14° at 20 m! |
| Target GPS (phone) | 5-10 m | same |
| Pitch | 1° | 17 m vertical at 1 km |

So heading dominates for far targets and GPS dominates for near ones.
That's why the HUD is a *direction cue*, not precision AR. The simulator's
error sliders show this.
