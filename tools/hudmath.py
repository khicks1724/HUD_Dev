"""
hudmath.py - Python reference implementation of the HUD geometry.

Mirrors firmware/components/hud_core (C) and web/hud_math.js (JS). The three
are checked against each other with tests/vectors.json:

    python tests/gen_vectors.py      # regenerate vectors from this file
    node tests/test_hud_math.js      # JS must match
    firmware/test_host/run_tests.*   # C must match

Conventions (see docs/HUD_MATH.md):
    ENU world frame, body frame x=right y=forward z=up,
    R = Rz(-heading) * Rx(pitch) * Ry(roll)  (body -> world)
"""
from __future__ import annotations

import math
from dataclasses import dataclass

WGS84_A = 6378137.0
WGS84_F = 1.0 / 298.257223563
WGS84_E2 = WGS84_F * (2.0 - WGS84_F)


def lla_to_ecef(lat_deg: float, lon_deg: float, hae_m: float):
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    s, c = math.sin(lat), math.cos(lat)
    n = WGS84_A / math.sqrt(1.0 - WGS84_E2 * s * s)
    return ((n + hae_m) * c * math.cos(lon), (n + hae_m) * c * math.sin(lon), (n * (1.0 - WGS84_E2) + hae_m) * s)


def lla_to_enu(obs, tgt):
    """obs/tgt = (lat, lon, hae). Returns (e, n, u) metres."""
    ox, oy, oz = lla_to_ecef(*obs)
    tx, ty, tz = lla_to_ecef(*tgt)
    dx, dy, dz = tx - ox, ty - oy, tz - oz
    lat, lon = math.radians(obs[0]), math.radians(obs[1])
    sl, cl, so, co = math.sin(lat), math.cos(lat), math.sin(lon), math.cos(lon)
    e = -so * dx + co * dy
    n = -sl * co * dx - sl * so * dy + cl * dz
    u = cl * co * dx + cl * so * dy + sl * dz
    return (e, n, u)


def enu_offset_to_lla(obs, e, n, u=0.0):
    """Small-offset inverse (flat-earth) used by the simulator to move units."""
    lat0 = math.radians(obs[0])
    m_per_deg_lat = 111132.92 - 559.82 * math.cos(2 * lat0) + 1.175 * math.cos(4 * lat0)
    m_per_deg_lon = 111412.84 * math.cos(lat0) - 93.5 * math.cos(3 * lat0)
    return (obs[0] + n / m_per_deg_lat, obs[1] + e / m_per_deg_lon, obs[2] + u)


def polar(enu):
    e, n, u = enu
    ground = math.hypot(e, n)
    return {
        "range": math.hypot(ground, u),
        "ground": ground,
        "bearing": math.degrees(math.atan2(e, n)) % 360.0,
        "elevation": math.degrees(math.atan2(u, ground)),
    }


# ---------------- quaternions (w, x, y, z), body -> world ----------------

def q_mul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    )


def q_axis_angle(ax, ay, az, ang):
    s = math.sin(ang / 2)
    return (math.cos(ang / 2), ax * s, ay * s, az * s)


def q_norm(q):
    n = math.sqrt(sum(c * c for c in q))
    return tuple(c / n for c in q)


def q_conj(q):
    return (q[0], -q[1], -q[2], -q[3])


def q_rotate(q, v):
    p = q_mul(q_mul(q, (0.0, *v)), q_conj(q))
    return p[1:]


def q_from_euler(heading, pitch, roll):
    qz = q_axis_angle(0, 0, 1, math.radians(-heading))
    qx = q_axis_angle(1, 0, 0, math.radians(pitch))
    qy = q_axis_angle(0, 1, 0, math.radians(roll))
    return q_norm(q_mul(q_mul(qz, qx), qy))


def q_to_euler(q):
    f = q_rotate(q, (0, 1, 0))
    r = q_rotate(q, (1, 0, 0))
    u = q_rotate(q, (0, 0, 1))
    return (
        math.degrees(math.atan2(f[0], f[1])) % 360.0,
        math.degrees(math.asin(max(-1.0, min(1.0, f[2])))),
        math.degrees(math.atan2(-r[2], u[2])),
    )


# ---------------- projection ----------------

@dataclass
class ProjCfg:
    width: int = 240
    height: int = 240
    hfov: float = 40.0
    vfov: float = 40.0
    margin: float = 10.0

    @property
    def cx(self):
        return (self.width - 1) / 2

    @property
    def cy(self):
        return (self.height - 1) / 2

    @property
    def fx(self):
        return (self.width / 2) / math.tan(math.radians(self.hfov / 2))

    @property
    def fy(self):
        return (self.height / 2) / math.tan(math.radians(self.vfov / 2))


def project(q, enu, cfg: ProjCfg):
    right, fwd, up = q_rotate(q_conj(q), enu)
    out = {
        "az": math.degrees(math.atan2(right, fwd)),
        "el": math.degrees(math.atan2(up, math.hypot(right, fwd))),
        "in_front": fwd > 1e-3,
        "on_screen": False,
        "sx": 0.0,
        "sy": 0.0,
    }
    if out["in_front"]:
        out["sx"] = cfg.cx + cfg.fx * right / fwd
        out["sy"] = cfg.cy - cfg.fy * up / fwd
        out["on_screen"] = 0 <= out["sx"] <= cfg.width - 1 and 0 <= out["sy"] <= cfg.height - 1
    return out
