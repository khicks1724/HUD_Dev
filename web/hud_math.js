/*
 * hud_math.js - JavaScript port of firmware/components/hud_core.
 *
 * Same conventions and algorithms as the C code (hud_geo.c, hud_attitude.c,
 * hud_projection.c, gfx.c, hud_render.c), so the web page's simulated HUD
 * draws what the device draws. Checked against tests/vectors.json by
 * tests/test_hud_math.js.
 *
 * Loaded as a classic script (works from file://) -> window.HudMath,
 * or require()'d from node.
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.HudMath = factory();
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";
  const D2R = Math.PI / 180, R2D = 180 / Math.PI;

  // ------------------------------------------------------------------ geo
  const A = 6378137.0, F = 1 / 298.257223563, E2 = F * (2 - F);

  function llaToEcef(lat, lon, h) {
    const la = lat * D2R, lo = lon * D2R, s = Math.sin(la), c = Math.cos(la);
    const n = A / Math.sqrt(1 - E2 * s * s);
    return [(n + h) * c * Math.cos(lo), (n + h) * c * Math.sin(lo), (n * (1 - E2) + h) * s];
  }

  function llaToEnu(obs, tgt) {
    const o = llaToEcef(obs[0], obs[1], obs[2]), t = llaToEcef(tgt[0], tgt[1], tgt[2]);
    const dx = t[0] - o[0], dy = t[1] - o[1], dz = t[2] - o[2];
    const la = obs[0] * D2R, lo = obs[1] * D2R;
    const sl = Math.sin(la), cl = Math.cos(la), so = Math.sin(lo), co = Math.cos(lo);
    return [-so * dx + co * dy, -sl * co * dx - sl * so * dy + cl * dz, cl * co * dx + cl * so * dy + sl * dz];
  }

  function enuOffsetToLla(obs, e, n, u) {
    const lat0 = obs[0] * D2R;
    const mLat = 111132.92 - 559.82 * Math.cos(2 * lat0) + 1.175 * Math.cos(4 * lat0);
    const mLon = 111412.84 * Math.cos(lat0) - 93.5 * Math.cos(3 * lat0);
    return [obs[0] + n / mLat, obs[1] + e / mLon, obs[2] + (u || 0)];
  }

  function wrap360(d) { d %= 360; return d < 0 ? d + 360 : d; }
  function wrap180(d) { d = wrap360(d); return d > 180 ? d - 360 : d; }

  function polar(enu) {
    const g = Math.hypot(enu[0], enu[1]);
    return { range: Math.hypot(g, enu[2]), ground: g, bearing: wrap360(Math.atan2(enu[0], enu[1]) * R2D),
      elevation: Math.atan2(enu[2], g) * R2D };
  }

  // ------------------------------------------------------------ quaternion
  // q = [w, x, y, z], body -> world. Body: x right, y forward, z up.
  function qmul(a, b) {
    return [
      a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
      a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
      a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
      a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0],
    ];
  }
  function qconj(q) { return [q[0], -q[1], -q[2], -q[3]]; }
  function qnorm(q) { const n = Math.hypot(q[0], q[1], q[2], q[3]) || 1; return q.map((v) => v / n); }
  function qaxis(ax, ay, az, ang) { const s = Math.sin(ang / 2); return [Math.cos(ang / 2), ax * s, ay * s, az * s]; }
  function qrot(q, v) {
    const tx = 2 * (q[2] * v[2] - q[3] * v[1]), ty = 2 * (q[3] * v[0] - q[1] * v[2]), tz = 2 * (q[1] * v[1] - q[2] * v[0]);
    return [v[0] + q[0] * tx + (q[2] * tz - q[3] * ty), v[1] + q[0] * ty + (q[3] * tx - q[1] * tz),
      v[2] + q[0] * tz + (q[1] * ty - q[2] * tx)];
  }
  function qrotInv(q, v) { return qrot(qconj(q), v); }
  function fromEuler(h, p, r) {
    return qnorm(qmul(qmul(qaxis(0, 0, 1, -h * D2R), qaxis(1, 0, 0, p * D2R)), qaxis(0, 1, 0, r * D2R)));
  }
  function toEuler(q) {
    const f = qrot(q, [0, 1, 0]), r = qrot(q, [1, 0, 0]), u = qrot(q, [0, 0, 1]);
    return { h: wrap360(Math.atan2(f[0], f[1]) * R2D), p: Math.asin(Math.max(-1, Math.min(1, f[2]))) * R2D,
      r: Math.atan2(-r[2], u[2]) * R2D };
  }

  // ------------------------------------------------------------ projection
  function projCfg(w, h, hfov, vfov) {
    return { width: w, height: h, cx: (w - 1) / 2, cy: (h - 1) / 2,
      fx: (w / 2) / Math.tan(hfov / 2 * D2R), fy: (h / 2) / Math.tan(vfov / 2 * D2R), margin: 10 };
  }

  function project(q, enu, cfg) {
    const b = qrotInv(q, enu), right = b[0], fwd = b[1], up = b[2];
    const o = { az: Math.atan2(right, fwd) * R2D, el: Math.atan2(up, Math.hypot(right, fwd)) * R2D,
      inFront: fwd > 1e-3, onScreen: false, sx: 0, sy: 0, body: b };
    if (o.inFront) {
      o.sx = cfg.cx + cfg.fx * right / fwd;
      o.sy = cfg.cy - cfg.fy * up / fwd;
      o.onScreen = o.sx >= 0 && o.sx <= cfg.width - 1 && o.sy >= 0 && o.sy <= cfg.height - 1;
    }
    let dx = right, dy = up;
    if (!o.inFront && Math.abs(dx) < 1e-6 && Math.abs(dy) < 1e-6) dy = -1;
    if (o.inFront) { dx = o.sx - cfg.cx; dy = cfg.cy - o.sy; }
    o.edgeAngle = Math.atan2(dy, dx) * R2D;
    const hw = cfg.width / 2 - cfg.margin, hh = cfg.height / 2 - cfg.margin;
    const s = Math.min(hw / Math.max(Math.abs(dx), 1e-9), hh / Math.max(Math.abs(dy), 1e-9));
    o.edgeX = cfg.cx + dx * s;
    o.edgeY = cfg.cy - dy * s;
    return o;
  }

  // ------------------------------------------------------------ CoT types
  const AFFIL = { UNKNOWN: 0, FRIEND: 1, HOSTILE: 2, NEUTRAL: 3 };
  const DIM = { OTHER: 0, GROUND: 1, AIR: 2, SEA: 3, SUB: 4, SPACE: 5 };
  function affilFromType(t) {
    if (!t || t[0] !== "a" || t[1] !== "-") return AFFIL.UNKNOWN;
    return { f: 1, a: 1, h: 2, s: 2, j: 2, k: 2, n: 3 }[t[2]] || 0;
  }
  function dimFromType(t) {
    if (!t || t[0] !== "a" || t.length < 5 || t[3] !== "-") return 0;
    return { G: 1, A: 2, S: 3, U: 4, P: 5 }[t[4]] || 0;
  }

  // ------------------------------------------------------------ gfx (port of gfx.c)
  const FONT = [
    [0, 0, 0, 0, 0], [0, 0, 0x5F, 0, 0], [0, 7, 0, 7, 0], [0x14, 0x7F, 0x14, 0x7F, 0x14], [0x24, 0x2A, 0x7F, 0x2A, 0x12],
    [0x23, 0x13, 0x08, 0x64, 0x62], [0x36, 0x49, 0x55, 0x22, 0x50], [0, 5, 3, 0, 0], [0, 0x1C, 0x22, 0x41, 0],
    [0, 0x41, 0x22, 0x1C, 0], [0x14, 0x08, 0x3E, 0x08, 0x14], [0x08, 0x08, 0x3E, 0x08, 0x08], [0, 0x50, 0x30, 0, 0],
    [8, 8, 8, 8, 8], [0, 0x60, 0x60, 0, 0], [0x20, 0x10, 0x08, 0x04, 0x02],
    [0x3E, 0x51, 0x49, 0x45, 0x3E], [0, 0x42, 0x7F, 0x40, 0], [0x42, 0x61, 0x51, 0x49, 0x46], [0x21, 0x41, 0x45, 0x4B, 0x31],
    [0x18, 0x14, 0x12, 0x7F, 0x10], [0x27, 0x45, 0x45, 0x45, 0x39], [0x3C, 0x4A, 0x49, 0x49, 0x30], [0x01, 0x71, 0x09, 0x05, 0x03],
    [0x36, 0x49, 0x49, 0x49, 0x36], [0x06, 0x49, 0x49, 0x29, 0x1E],
    [0, 0x36, 0x36, 0, 0], [0, 0x56, 0x36, 0, 0], [0x08, 0x14, 0x22, 0x41, 0], [0x14, 0x14, 0x14, 0x14, 0x14],
    [0, 0x41, 0x22, 0x14, 0x08], [0x02, 0x01, 0x51, 0x09, 0x06], [0x32, 0x49, 0x79, 0x41, 0x3E],
    [0x7E, 0x11, 0x11, 0x11, 0x7E], [0x7F, 0x49, 0x49, 0x49, 0x36], [0x3E, 0x41, 0x41, 0x41, 0x22], [0x7F, 0x41, 0x41, 0x22, 0x1C],
    [0x7F, 0x49, 0x49, 0x49, 0x41], [0x7F, 0x09, 0x09, 0x09, 0x01], [0x3E, 0x41, 0x49, 0x49, 0x7A], [0x7F, 0x08, 0x08, 0x08, 0x7F],
    [0, 0x41, 0x7F, 0x41, 0], [0x20, 0x40, 0x41, 0x3F, 0x01], [0x7F, 0x08, 0x14, 0x22, 0x41], [0x7F, 0x40, 0x40, 0x40, 0x40],
    [0x7F, 0x02, 0x0C, 0x02, 0x7F], [0x7F, 0x04, 0x08, 0x10, 0x7F], [0x3E, 0x41, 0x41, 0x41, 0x3E], [0x7F, 0x09, 0x09, 0x09, 0x06],
    [0x3E, 0x41, 0x51, 0x21, 0x5E], [0x7F, 0x09, 0x19, 0x29, 0x46], [0x46, 0x49, 0x49, 0x49, 0x31], [0x01, 0x01, 0x7F, 0x01, 0x01],
    [0x3F, 0x40, 0x40, 0x40, 0x3F], [0x1F, 0x20, 0x40, 0x20, 0x1F], [0x3F, 0x40, 0x38, 0x40, 0x3F], [0x63, 0x14, 0x08, 0x14, 0x63],
    [0x07, 0x08, 0x70, 0x08, 0x07], [0x61, 0x51, 0x49, 0x45, 0x43],
    [0, 0x7F, 0x41, 0x41, 0], [0x02, 0x04, 0x08, 0x10, 0x20], [0, 0x41, 0x41, 0x7F, 0], [0x04, 0x02, 0x01, 0x02, 0x04],
    [0x40, 0x40, 0x40, 0x40, 0x40], [0, 0x01, 0x02, 0x04, 0],
    [0x20, 0x54, 0x54, 0x54, 0x78], [0x7F, 0x48, 0x44, 0x44, 0x38], [0x38, 0x44, 0x44, 0x44, 0x20], [0x38, 0x44, 0x44, 0x48, 0x7F],
    [0x38, 0x54, 0x54, 0x54, 0x18], [0x08, 0x7E, 0x09, 0x01, 0x02], [0x0C, 0x52, 0x52, 0x52, 0x3E], [0x7F, 0x08, 0x04, 0x04, 0x78],
    [0, 0x44, 0x7D, 0x40, 0], [0x20, 0x40, 0x44, 0x3D, 0], [0x7F, 0x10, 0x28, 0x44, 0], [0, 0x41, 0x7F, 0x40, 0],
    [0x7C, 0x04, 0x18, 0x04, 0x78], [0x7C, 0x08, 0x04, 0x04, 0x78], [0x38, 0x44, 0x44, 0x44, 0x38], [0x7C, 0x14, 0x14, 0x14, 0x08],
    [0x08, 0x14, 0x14, 0x18, 0x7C], [0x7C, 0x08, 0x04, 0x04, 0x08], [0x48, 0x54, 0x54, 0x54, 0x20], [0x04, 0x3F, 0x44, 0x40, 0x20],
    [0x3C, 0x40, 0x40, 0x20, 0x7C], [0x1C, 0x20, 0x40, 0x20, 0x1C], [0x3C, 0x40, 0x30, 0x40, 0x3C], [0x44, 0x28, 0x10, 0x28, 0x44],
    [0x0C, 0x50, 0x50, 0x50, 0x3C], [0x44, 0x64, 0x54, 0x4C, 0x44],
    [0, 0x08, 0x36, 0x41, 0], [0, 0, 0x7F, 0, 0], [0, 0x41, 0x36, 0x08, 0], [0x08, 0x04, 0x08, 0x10, 0x08], [0, 0x06, 0x09, 0x09, 0x06],
  ];

  const rgb = (r, g, b) => ((255 << 24) | (b << 16) | (g << 8) | r) >>> 0; // little-endian RGBA in a Uint32
  const C = {
    BLACK: rgb(0, 0, 0), WHITE: rgb(255, 255, 255), ACCENT: rgb(255, 255, 255), ACCENT_DIM: rgb(125, 125, 125),
    CYAN: rgb(80, 200, 255), RED: rgb(255, 60, 60), LIME: rgb(140, 255, 120), YELLOW: rgb(255, 230, 60), AMBER: rgb(255, 170, 0),
  };

  class Gfx {
    constructor(w, h) { this.w = w; this.h = h; this.px = new Uint32Array(w * h); }
    clear(c) { this.px.fill(c); }
    pixel(x, y, c) { x |= 0; y |= 0; if (x >= 0 && y >= 0 && x < this.w && y < this.h) this.px[y * this.w + x] = c; }
    hline(x, y, w, c) { for (let i = 0; i < w; i++) this.pixel(x + i, y, c); }
    vline(x, y, h, c) { for (let i = 0; i < h; i++) this.pixel(x, y + i, c); }
    line(x0, y0, x1, y1, c) {
      x0 |= 0; y0 |= 0; x1 |= 0; y1 |= 0;
      const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
      let err = dx + dy;
      for (let g = 0; g < 2048; g++) {
        this.pixel(x0, y0, c);
        if (x0 === x1 && y0 === y1) break;
        const e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
      }
    }
    rect(x, y, w, h, c) { this.hline(x, y, w, c); this.hline(x, y + h - 1, w, c); this.vline(x, y, h, c); this.vline(x + w - 1, y, h, c); }
    fillRect(x, y, w, h, c) { for (let j = 0; j < h; j++) this.hline(x, y + j, w, c); }
    circle(cx, cy, r, c) {
      let x = r, y = 0, err = 1 - r;
      while (x >= y) {
        [[x, y], [y, x], [-y, x], [-x, y], [-x, -y], [-y, -x], [y, -x], [x, -y]].forEach(([a, b]) => this.pixel(cx + a, cy + b, c));
        y++;
        if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x) + 1; }
      }
    }
    diamond(cx, cy, r, c) { this.line(cx, cy - r, cx + r, cy, c); this.line(cx + r, cy, cx, cy + r, c); this.line(cx, cy + r, cx - r, cy, c); this.line(cx - r, cy, cx, cy - r, c); }
    triangle(x0, y0, x1, y1, x2, y2, c) { this.line(x0, y0, x1, y1, c); this.line(x1, y1, x2, y2, c); this.line(x2, y2, x0, y0, c); }
    text(x, y, s, c, scale) {
      scale = scale || 1;
      for (const chr of s) {
        let code = chr.charCodeAt(0);
        if (chr === "°") code = 0x7f;
        if (code < 0x20 || code > 0x7f) code = 63;
        const g = FONT[code - 0x20];
        for (let col = 0; col < 5; col++) {
          let bits = g[col];
          for (let row = 0; row < 7; row++, bits >>= 1) {
            if (bits & 1) { if (scale === 1) this.pixel(x + col, y + row, c); else this.fillRect(x + col * scale, y + row * scale, scale, scale, c); }
          }
        }
        x += 6 * scale;
      }
    }
    textWidth(s, scale) { return s.length ? (s.length * 6 - 1) * (scale || 1) : 0; }
    textC(x, y, s, c, scale) { this.text(x - ((this.textWidth(s, scale) / 2) | 0), y, s, c, scale); }
    toImageData(ctx) { const id = ctx.createImageData(this.w, this.h); new Uint32Array(id.data.buffer).set(this.px); return id; }
  }

  // ------------------------------------------------------------ renderer (port of hud_render.c)
  const MODE = { NORMAL: 0, MINIMAL: 1, CALIB: 2, STATUS: 3 };
  const THERMAL = { OFF: 0, FULL: 1, HOT: 2 };
  const TAPE_H = 22;

  function formatRange(m) {
    if (m < 1000) return `${Math.round(m)}m`;
    if (m < 10000) return `${(m / 1000).toFixed(1)}km`;
    return `${Math.round(m / 1000)}km`;
  }
  function affilColor(a) { return [C.YELLOW, C.CYAN, C.RED, C.LIME][a] || C.YELLOW; }
  function dirEnu(b, e) { b *= D2R; e *= D2R; return [Math.sin(b) * Math.cos(e), Math.cos(b) * Math.cos(e), Math.sin(e)]; }

  function drawSymbol(g, x, y, a, d, c, stale) {
    const r = 6;
    if (a === AFFIL.FRIEND) {
      if (d === DIM.AIR) {
        for (let i = 0; i < 180; i += 15) {
          const t0 = i * D2R, t1 = (i + 15) * D2R;
          g.line(x + (r * Math.cos(t0) | 0), y - (r * Math.sin(t0) | 0), x + (r * Math.cos(t1) | 0), y - (r * Math.sin(t1) | 0), c);
        }
        g.hline(x - r, y, 2 * r + 1, c);
      } else g.rect(x - r - 2, y - r + 1, 2 * r + 5, 2 * r - 1, c);
    } else if (a === AFFIL.HOSTILE) {
      g.diamond(x, y, r + 1, c);
      if (d === DIM.AIR) g.hline(x - r - 1, y + r + 3, 2 * r + 3, c);
    } else if (a === AFFIL.NEUTRAL) g.rect(x - r, y - r, 2 * r + 1, 2 * r + 1, c);
    else { g.circle(x, y, r, c); g.pixel(x, y, c); }
    if (!stale) g.fillRect(x - 1, y - 1, 3, 3, c);
  }

  function drawTape(g, s, heading) {
    const p = s.proj, half = Math.atan((p.width * 0.5) / p.fx) * R2D;
    for (let d = Math.floor((heading - half) / 5) * 5; d <= heading + half; d += 5) {
      const x = Math.round(p.cx + p.fx * Math.tan((d - heading) * D2R));
      const hd = ((d % 360) + 360) % 360;
      if (hd % 10 === 0) {
        g.vline(x, 0, 6, C.ACCENT);
        if (Math.abs(x - (p.cx | 0)) > 18) {
          const card = { 0: "N", 90: "E", 180: "S", 270: "W" }[hd];
          g.textC(x, 8, card || String(hd / 10), C.ACCENT, 1);
        }
      } else g.vline(x, 0, 3, C.ACCENT_DIM);
    }
    const cx = p.cx | 0;
    g.fillRect(cx - 13, 7, 27, 11, C.BLACK);
    g.rect(cx - 13, 6, 27, 13, C.ACCENT);
    g.textC(cx + 1, 9, String(Math.round(heading) % 360).padStart(3, "0"), C.WHITE, 1);
    g.triangle(cx - 3, TAPE_H - 2, cx + 3, TAPE_H - 2, cx, TAPE_H + 1, C.ACCENT);
  }

  function drawWorldLine(g, s, heading, el, halfw, c, dashed) {
    const a = project(s.q, dirEnu(heading - halfw, el), s.proj), b = project(s.q, dirEnu(heading + halfw, el), s.proj);
    if (!a.inFront || !b.inFront) return;
    if (!dashed) { g.line(a.sx, a.sy, b.sx, b.sy, c); return; }
    for (let i = 0; i < 6; i += 2) {
      const t0 = i / 6, t1 = (i + 1) / 6;
      g.line(a.sx + (b.sx - a.sx) * t0, a.sy + (b.sy - a.sy) * t0, a.sx + (b.sx - a.sx) * t1, a.sy + (b.sy - a.sy) * t1, c);
    }
  }

  function drawHorizon(g, s, heading) {
    const P = (h) => project(s.q, dirEnu(h, 0), s.proj);
    const a = P(heading - 60), b = P(heading - 4), c = P(heading + 4), d = P(heading + 60);
    if (a.inFront && b.inFront) g.line(a.sx, a.sy, b.sx, b.sy, C.ACCENT_DIM);
    if (c.inFront && d.inFront) g.line(c.sx, c.sy, d.sx, d.sy, C.ACCENT_DIM);
    for (let el = -20; el <= 20; el += 10) if (el) drawWorldLine(g, s, heading, el, 3, C.ACCENT_DIM, el < 0);
  }

  function drawBoresight(g, s) {
    const x = Math.round(s.proj.cx), y = Math.round(s.proj.cy);
    g.hline(x - 10, y, 7, C.ACCENT); g.hline(x + 4, y, 7, C.ACCENT); g.vline(x, y - 10, 7, C.ACCENT); g.vline(x, y + 4, 7, C.ACCENT);
  }

  function drawRadar(g, s, t, heading) {
    const R = 30, cx = g.w - R - 4, cy = g.h - R - 4;
    g.circle(cx, cy, R, C.ACCENT_DIM); g.pixel(cx, cy, C.ACCENT);
    const half = Math.atan((s.proj.width * 0.5) / s.proj.fx);
    g.line(cx, cy, cx + (R * Math.sin(-half) | 0), cy - (R * Math.cos(half) | 0), C.ACCENT_DIM);
    g.line(cx, cy, cx + (R * Math.sin(half) | 0), cy - (R * Math.cos(half) | 0), C.ACCENT_DIM);
    const h = heading * D2R, ch = Math.cos(h), sh = Math.sin(h);
    for (const tg of t) {
      const x = tg.enu[0] * ch - tg.enu[1] * sh, y = tg.enu[0] * sh + tg.enu[1] * ch, d = Math.hypot(x, y);
      let k = R / s.radarRange;
      if (d * k > R - 2) k = (R - 2) / d;
      g.fillRect(cx + (x * k | 0) - 1, cy - (y * k | 0) - 1, 3, 3, affilColor(tg.affil));
    }
  }

  function drawStatusBar(g, s, shown, total) {
    const lc = s.link === 2 ? C.ACCENT : s.link === 1 ? C.AMBER : C.RED;
    g.text(3, g.h - 30, s.link === 2 ? "TAK" : s.link === 1 ? "WIFI" : "NOLINK", lc, 1);
    g.text(3, g.h - 20, `POS ${s.ownValid ? s.posSource : "----"}`, s.ownValid ? C.ACCENT : C.RED, 1);
    g.text(3, g.h - 10, `HDG ${s.hdgSource}`, C.ACCENT, 1);
    const txt = `${shown}/${total}`;
    g.text(g.w - 70 - g.textWidth(txt, 1), g.h - 10, txt, C.ACCENT, 1);
  }

  function drawTargets(g, s, t, labels) {
    const items = [];
    for (const tg of t) {
      const r = Math.hypot(tg.enu[0], tg.enu[1], tg.enu[2]);
      if (s.maxRange > 0 && r > s.maxRange) continue;
      items.push({ tg, r });
    }
    items.sort((a, b) => b.r - a.r);
    let shown = 0;
    items.forEach(({ tg, r }, k) => {
      const stale = (tg.age || 0) > 30, c = stale ? C.ACCENT_DIM : affilColor(tg.affil);
      const p = project(s.q, tg.enu, s.proj);
      const label = labels && (items.length - k) <= s.maxLabels;
      const rng = formatRange(r);
      tg._proj = p;
      if (p.onScreen) {
        const x = Math.round(p.sx), y = Math.round(p.sy);
        if (y < TAPE_H + 4) return;
        drawSymbol(g, x, y, tg.affil, tg.dim, c, stale);
        if (tg.selected) g.rect(x - 11, y - 11, 23, 23, C.WHITE);
        if (label) { g.textC(x, y + 10, tg.callsign || "?", c, 1); g.textC(x, y + 19, rng, c, 1); }
        shown++;
      } else {
        const a = p.edgeAngle * D2R, ux = Math.cos(a), uy = -Math.sin(a), ex = p.edgeX | 0, ey = p.edgeY | 0;
        if (ey < TAPE_H + 4 && uy < 0) return;
        g.triangle(ex + (ux * 7 | 0), ey + (uy * 7 | 0), ex + (-uy * 5 | 0), ey + (ux * 5 | 0), ex - (-uy * 5 | 0), ey - (ux * 5 | 0), c);
        if (label) g.textC(ex - (ux * 18 | 0), ey - (uy * 14 | 0) - 3, rng, c, 1);
      }
    });
    return shown;
  }

  function drawCalib(g, s) {
    const p = s.proj, cx = Math.round(p.cx), cy = Math.round(p.cy);
    g.hline(0, cy, g.w, C.ACCENT); g.vline(cx, 0, g.h, C.ACCENT);
    for (let deg = -30; deg <= 30; deg += 5) {
      if (!deg) continue;
      const dx = p.fx * Math.tan(deg * D2R) | 0, dy = p.fy * Math.tan(deg * D2R) | 0, len = deg % 10 === 0 ? 8 : 4;
      g.vline(cx + dx, cy - len / 2, len, C.ACCENT); g.hline(cx - len / 2, cy - dy, len, C.ACCENT);
      if (deg % 10 === 0) { g.textC(cx + dx, cy + 6, String(deg), C.ACCENT_DIM, 1); g.text(cx + 6, cy - dy - 3, String(deg), C.ACCENT_DIM, 1); }
    }
    g.circle(cx, cy, 20, C.WHITE);
    g.textC(g.w / 2, 4, "BORESIGHT CAL", C.WHITE, 1);
    g.textC(g.w / 2, g.h - 12, "AIM + HOLD BTN", C.AMBER, 1);
  }

  function drawThermal(g, th, mode) {
    if (!th || !th.px) return;
    const hot = mode === THERMAL.HOT;
    for (let y = 0; y < g.h; y++) {
      const sy = th.srcY + (y * th.srcH / g.h | 0);
      if (sy < 0 || sy >= th.h) continue;
      for (let x = 0; x < g.w; x++) {
        const sx = th.srcX + (x * th.srcW / g.w | 0);
        if (sx < 0 || sx >= th.w) continue;
        let v = th.px[sy * th.w + sx];
        if (hot) { if (v < th.hot) continue; v = 120 + ((v - th.hot) * 135 / (256 - th.hot) | 0); } else v = v * 3 / 4 | 0;
        g.px[y * g.w + x] = rgb(v, v, v); // white-hot, black-cold
      }
    }
  }

  /** scene: {mode, q, proj, ownValid, posSource, hdgSource, link, maxRange, radarRange, maxLabels,
   *          thermalMode, thermal, statusLines}; targets: [{enu, affil, dim, callsign, age, selected}] */
  function render(g, s, t) {
    g.clear(C.BLACK);
    if (s.thermalMode && s.mode !== MODE.CALIB && s.mode !== MODE.STATUS) drawThermal(g, s.thermal, s.thermalMode);
    const e = toEuler(s.q);
    if (s.mode === MODE.CALIB) return drawCalib(g, s);
    if (s.mode === MODE.STATUS) {
      g.text(4, 4, "HUD STATUS", C.WHITE, 2);
      (s.statusLines || []).forEach((l, i) => g.text(4, 26 + i * 12, l, C.ACCENT, 1));
      return;
    }
    if (s.mode === MODE.MINIMAL) { drawBoresight(g, s); drawTargets(g, s, t, true); return; }
    drawHorizon(g, s, e.h);
    drawTape(g, s, e.h);
    drawBoresight(g, s);
    const shown = drawTargets(g, s, t, true);
    drawRadar(g, s, t, e.h);
    drawStatusBar(g, s, shown, t.length);
    if (!s.ownValid) g.textC(g.w / 2, g.h / 2 + 30, "NO OWN POSITION", C.RED, 1);
  }

  return { D2R, R2D, llaToEcef, llaToEnu, enuOffsetToLla, polar, wrap360, wrap180,
    qmul, qconj, qnorm, qaxis, qrot, qrotInv, fromEuler, toEuler, projCfg, project,
    AFFIL, DIM, affilFromType, dimFromType, Gfx, COLORS: C, MODE, THERMAL, render, formatRange };
});
