// node tests/test_hud_math.js  - checks web/hud_math.js against tests/vectors.json
// (generated from tools/hudmath.py, the same vectors the C host tests use).
const path = require("path");
const H = require(path.join(__dirname, "..", "web", "hud_math.js"));
const V = require(path.join(__dirname, "vectors.json"));

let pass = 0, fail = 0;
const near = (a, b, tol) => Math.abs(a - b) <= tol;
function check(ok, msg) { if (ok) pass++; else { fail++; console.log("FAIL", msg); } }

const cfg = H.projCfg(V.cfg.width, V.cfg.height, V.cfg.hfov, V.cfg.vfov);
V.cases.forEach((c, i) => {
  const enu = H.llaToEnu(c.obs, c.tgt);
  check(enu.every((v, k) => near(v, c.enu[k], 1e-6)), `case ${i} enu`);
  const p = H.polar(enu);
  check(near(p.bearing, c.polar.bearing, 1e-9) && near(p.range, c.polar.range, 1e-6), `case ${i} polar`);
  const q = H.fromEuler(...c.att);
  check(q.every((v, k) => near(v, c.q[k], 1e-9)), `case ${i} quat`);
  const e = H.toEuler(q);
  check(Math.abs(H.wrap180(e.h - c.att[0])) < 1e-6 && near(e.p, c.att[1], 1e-6) && near(e.r, c.att[2], 1e-6), `case ${i} euler`);
  const pr = H.project(q, enu, cfg);
  check(pr.inFront === c.proj.in_front, `case ${i} inFront`);
  if (c.proj.in_front) check(near(pr.sx, c.proj.sx, 1e-6) && near(pr.sy, c.proj.sy, 1e-6), `case ${i} proj`);
});

// Renderer smoke test: draws without throwing and lights some pixels.
const g = new H.Gfx(240, 240);
H.render(g, { mode: 0, q: H.fromEuler(10, 2, 3), proj: cfg, ownValid: true, posSource: "SIM", hdgSource: "SIM", link: 2,
  maxRange: 5000, radarRange: 2000, maxLabels: 6 },
  [{ enu: [100, 500, 0], affil: 1, dim: 1, callsign: "A1", age: 1 }, { enu: [900, -100, 0], affil: 2, dim: 2, callsign: "H", age: 1 }]);
check(g.px.some((v) => v !== H.COLORS.BLACK), "renderer drew pixels");

console.log(`${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
