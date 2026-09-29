/*
 * app.js - page logic: scenario, simulator, synthetic thermal, math
 * breakdown, live view (Web Serial + Wi-Fi), UI wiring.
 * Classic script (works from file://). 3D lives in scene3d.js.
 */
(function () {
  "use strict";
  const H = window.HudMath;
  const $ = (id) => document.getElementById(id);
  const App = (window.App = {});

  // ================================================================ scenario
  // Mirrors tools/sim_server.py and firmware/main/fake_targets.c.
  const OWNER_UID = "ANDROID-SIM-OWNER";
  function scenarioUnits(t, o) {
    const out = [];
    const add = (uid, cs, type, e, n, u, course, speed, team) => {
      const [lat, lon, hae] = H.enuOffsetToLla(o, e, n, u);
      out.push({ uid, cs, type, lat, lon, hae, course: H.wrap360(course), speed, team: team || "Cyan" });
    };
    const a = (t * 1.4) / 150;
    add("SIM-ALPHA1", "ALPHA1", "a-f-G-U-C-I", 150 * Math.sin(a), 350 + 150 * Math.cos(a), 0, (a * 180) / Math.PI + 90, 1.4);
    const x = -385 + 300 * Math.sin(t / 40);
    add("SIM-BRAVO3", "BRAVO3", "a-f-G-E-V", x, 460, 0, Math.cos(t / 40) > 0 ? 90 : 270, 7.5);
    const b = (t * 25) / 600;
    add("SIM-UAV12", "UAV12", "a-f-A-M-F-Q", 1100 + 600 * Math.cos(b), 1100 + 600 * Math.sin(b), 450, (-b * 180) / Math.PI, 25);
    add("SIM-H1", "TGT-H1", "a-h-G-U-C", 60, 1400, 8, 0, 0, "Red");
    add("SIM-N1", "CIV", "a-n-G", -900, 300, 0, 0, 0, "White");
    add("SIM-U1", "UNK", "a-u-G", 900, -700, 0, 0, 0, "Yellow");
    add(OWNER_UID, "KYLE", "a-f-G-U-C", 20 * Math.sin(t / 30), 20 * Math.cos(t / 45), 0, 0, 0.8);
    return out;
  }

  // ================================================================ state
  const S = (App.state = {
    origin: [36.5967, -121.875, 20],
    t0: performance.now(),
    source: "builtin", // builtin | server
    serverUrl: "",
    serverUnits: null,
    units: [],
    att: { h: 8, p: 2, r: 0 },
    scan: false,
    hfov: 40,
    vfov: 40,
    mode: 0,
    thermal: 0,
    hdgErr: 0,
    posErr: 0,
    mirror: false,
    selected: "SIM-H1",
    maxRange: 5000,
    radarRange: 2000,
    world: null, // computed each frame, read by scene3d.js
  });

  // Synthetic thermal camera: Boson 640 geometry at half resolution (320x256), 50 deg HFOV.
  const TH = { w: 320, h: 256, hfov: 50 };
  TH.vfov = 2 * Math.atan(Math.tan((TH.hfov / 2) * H.D2R) * (TH.h / TH.w)) * H.R2D;
  TH.buf = new Uint8Array(TH.w * TH.h);
  TH.cfg = H.projCfg(TH.w, TH.h, TH.hfov, TH.vfov);

  function thermalCrop(hfov, vfov) {
    const fx = TH.w / 2 / Math.tan((TH.hfov / 2) * H.D2R);
    const w = Math.min(TH.w, Math.round(2 * fx * Math.tan((hfov / 2) * H.D2R)));
    const h = Math.min(TH.h, Math.round(2 * fx * Math.tan((vfov / 2) * H.D2R)));
    return { srcX: ((TH.w - w) / 2) | 0, srcY: ((TH.h - h) / 2) | 0, srcW: w, srcH: h };
  }

  // Render a plausible thermal frame from the TRUE geometry (the camera sees
  // the truth even when the HUD's heading/position are off).
  function synthThermal(qTrue, units, ownTrue) {
    const b = TH.buf, cfg = TH.cfg;
    const e = H.toEuler(qTrue);
    const p1 = H.project(qTrue, dir(e.h - 20, 0), cfg), p2 = H.project(qTrue, dir(e.h + 20, 0), cfg);
    const slope = p1.inFront && p2.inFront ? (p2.sy - p1.sy) / (p2.sx - p1.sx || 1e-6) : 0;
    const y0 = p1.inFront ? p1.sy - slope * p1.sx : e.p > 0 ? TH.h * 2 : -TH.h;
    for (let y = 0; y < TH.h; y++) {
      for (let x = 0; x < TH.w; x++) {
        const hy = y0 + slope * x;
        const v = y < hy ? 30 + Math.min(40, (y / TH.h) * 40) : 90 + Math.min(45, ((y - hy) / TH.h) * 60);
        b[y * TH.w + x] = v + ((x * 7 + y * 13) % 5);
      }
    }
    for (const u of units) {
      const enu = H.llaToEnu(ownTrue, [u.lat, u.lon, u.hae]);
      const p = H.project(qTrue, enu, cfg);
      if (!p.inFront) continue;
      const r = Math.hypot(enu[0], enu[1], enu[2]);
      const isVeh = u.type.startsWith("a-f-G-E") || H.dimFromType(u.type) === H.DIM.AIR;
      const hm = isVeh ? 2.2 : 1.8, wm = isVeh ? 3.5 : 0.6;
      const hh = Math.max(1, (cfg.fy * hm) / r / 2), hw = Math.max(1, (cfg.fx * wm) / r / 2);
      const temp = isVeh ? 250 : 232;
      for (let dy = -Math.ceil(hh); dy <= Math.ceil(hh); dy++)
        for (let dx = -Math.ceil(hw); dx <= Math.ceil(hw); dx++) {
          const xx = Math.round(p.sx + dx), yy = Math.round(p.sy + dy);
          if (xx < 0 || yy < 0 || xx >= TH.w || yy >= TH.h) continue;
          const k = (dx * dx) / (hw * hw + 0.5) + (dy * dy) / (hh * hh + 0.5);
          if (k <= 1.2) b[yy * TH.w + xx] = Math.max(b[yy * TH.w + xx], temp - k * 25);
        }
    }
  }
  const dir = (bearing, el) => {
    const bb = bearing * H.D2R, ee = el * H.D2R;
    return [Math.sin(bb) * Math.cos(ee), Math.cos(bb) * Math.cos(ee), Math.sin(ee)];
  };

  // ================================================================ simulator step
  const hudCanvas = () => $("hudCanvas");
  const gfx = new H.Gfx(240, 240);
  const typeName = (t) => ["Unknown", "Friend", "Hostile", "Neutral"][H.affilFromType(t)];

  function step() {
    const t = (performance.now() - S.t0) / 1000;
    S.units = S.source === "server" && S.serverUnits ? S.serverUnits : scenarioUnits(t, S.origin);

    const owner = S.units.find((u) => u.uid === OWNER_UID) || { lat: S.origin[0], lon: S.origin[1], hae: S.origin[2] };
    const ownTrue = [owner.lat, owner.lon, owner.hae + 1.7];
    // GPS error: displace the HUD's belief of its own position.
    const errDir = 45 * H.D2R;
    const ownHud = H.enuOffsetToLla(ownTrue, S.posErr * Math.sin(errDir), S.posErr * Math.cos(errDir), 0);

    let { h, p, r } = S.att;
    if (S.scan) {
      h = H.wrap360(S.att.h + 35 * Math.sin(t * 0.35));
      p = S.att.p + 4 * Math.sin(t * 0.5);
    }
    const qTrue = H.fromEuler(h, p, r);
    const qHud = H.fromEuler(h + S.hdgErr, p, r);
    const proj = H.projCfg(240, 240, S.hfov, S.vfov);

    const others = S.units.filter((u) => u.uid !== OWNER_UID);
    const targets = others.map((u) => ({
      uid: u.uid,
      enu: H.llaToEnu(ownHud, [u.lat, u.lon, u.hae]),
      affil: H.affilFromType(u.type),
      dim: H.dimFromType(u.type),
      callsign: u.cs,
      age: 1,
      selected: u.uid === S.selected,
      unit: u,
    }));

    let thermal = null;
    if (S.thermal) {
      synthThermal(qTrue, others, ownTrue);
      thermal = Object.assign({ px: TH.buf, w: TH.w, h: TH.h, hot: 170 }, thermalCrop(S.hfov, S.vfov));
    }

    const scene = {
      mode: S.mode, q: qHud, proj, ownValid: true, posSource: S.source === "server" ? "TAK" : "SIM",
      hdgSource: S.hdgErr ? "GYRO" : "MAG", link: 2, maxRange: S.maxRange, radarRange: S.radarRange, maxLabels: 6,
      thermalMode: S.thermal, thermal,
      statusLines: [`SRC ${S.source}`, `OWN ${ownHud[0].toFixed(5)} ${ownHud[1].toFixed(5)}`, `HDG ${h.toFixed(1)} P ${p.toFixed(1)} R ${r.toFixed(1)}`,
        `FOV ${S.hfov}x${S.vfov}`, `TRACKS ${targets.length}`, `ERR hdg ${S.hdgErr} pos ${S.posErr}m`],
    };
    H.render(gfx, scene, targets);
    drawHud(hudCanvas(), gfx, false);
    const lcd = $("lcdCanvas");
    if (lcd) drawHud(lcd, gfx, true);

    S.world = { t, ownTrue, ownHud, qTrue, qHud, units: S.units, others, targets, proj, att: { h, p, r } };
    updateMath(targets, qHud, ownHud, proj);
    updateTable(targets);
    $("attReadout").textContent = `HDG ${h.toFixed(1)}°  PITCH ${p.toFixed(1)}°  ROLL ${r.toFixed(1)}°`;
  }

  // Draw a Gfx buffer to a canvas (optionally mirrored like the LCD behind the prism).
  const tmp = document.createElement("canvas");
  tmp.width = tmp.height = 240;
  function drawHud(canvas, g, mirror) {
    if (!canvas) return;
    const tctx = tmp.getContext("2d");
    tctx.putImageData(g.toImageData(tctx), 0, 0);
    const ctx = canvas.getContext("2d");
    ctx.imageSmoothingEnabled = false;
    ctx.save();
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    if (mirror) { ctx.translate(canvas.width, 0); ctx.scale(-1, 1); }
    ctx.drawImage(tmp, 0, 0, canvas.width, canvas.height);
    ctx.restore();
  }
  App.drawHud = drawHud;

  // ================================================================ math breakdown
  function fmt(v, d) { return (v >= 0 ? " " : "") + v.toFixed(d); }
  function updateMath(targets, q, own, proj) {
    const el = $("mathSteps");
    if (!el || el.offsetParent === null) return;
    const tg = targets.find((t) => t.uid === S.selected) || targets[0];
    if (!tg) return;
    const u = tg.unit;
    const enu = tg.enu, pol = H.polar(enu), b = H.qrotInv(q, enu), pr = H.project(q, enu, proj), e = H.toEuler(q);
    el.innerHTML = `
      <div class="step"><b>1 · Positions (WGS-84, HAE)</b><code>own    ${own[0].toFixed(6)}, ${own[1].toFixed(6)}, ${own[2].toFixed(1)} m
target ${u.lat.toFixed(6)}, ${u.lon.toFixed(6)}, ${u.hae.toFixed(1)} m   (${u.cs}, ${u.type})</code></div>
      <div class="step"><b>2 · ECEF difference → local ENU at own position</b><code>E ${fmt(enu[0], 1)} m   N ${fmt(enu[1], 1)} m   U ${fmt(enu[2], 1)} m</code></div>
      <div class="step"><b>3 · Polar</b><code>bearing ${pol.bearing.toFixed(2)}°   elevation ${pol.elevation.toFixed(2)}°   range ${pol.range.toFixed(0)} m</code></div>
      <div class="step"><b>4 · Rotate into the HUD frame</b> <span class="muted">v_body = q⁻¹ · v_enu, attitude hdg ${e.h.toFixed(1)} / pitch ${e.p.toFixed(1)} / roll ${e.r.toFixed(1)}</span><code>right ${fmt(b[0], 1)}   forward ${fmt(b[1], 1)}   up ${fmt(b[2], 1)}   → ${pr.az.toFixed(2)}° ${pr.az >= 0 ? "right" : "left"}, ${Math.abs(pr.el).toFixed(2)}° ${pr.el >= 0 ? "up" : "down"}</code></div>
      <div class="step"><b>5 · Pinhole projection</b> <span class="muted">fx = 120 / tan(${S.hfov / 2}°) = ${proj.fx.toFixed(1)} px</span><code>sx = ${proj.cx} + ${proj.fx.toFixed(1)} · right/forward = ${pr.inFront ? pr.sx.toFixed(1) : "—"}
sy = ${proj.cy} − ${proj.fy.toFixed(1)} · up/forward    = ${pr.inFront ? pr.sy.toFixed(1) : "—"}
${!pr.inFront ? "behind the viewer → edge cue" : pr.onScreen ? "on screen" : "outside the display → edge cue at (" + pr.edgeX.toFixed(0) + ", " + pr.edgeY.toFixed(0) + ")"}</code></div>`;
  }

  function updateTable(targets) {
    const tb = $("targetRows");
    if (!tb || tb.offsetParent === null) return;
    if (!App._tableTick || performance.now() - App._tableTick > 400) {
      App._tableTick = performance.now();
      tb.innerHTML = targets
        .map((t) => {
          const p = H.polar(t.enu), pr = t._proj || {};
          return `<tr data-uid="${t.uid}" class="${t.uid === S.selected ? "sel" : ""}">
            <td><span class="dot a${t.affil}"></span>${t.callsign}</td><td>${typeName(t.unit.type)}</td>
            <td>${p.bearing.toFixed(0)}°</td><td>${H.formatRange(p.range)}</td><td>${pr.onScreen ? "on HUD" : pr.inFront ? "edge" : "behind"}</td></tr>`;
        })
        .join("");
    }
  }

  // ================================================================ sim server feed
  let es = null;
  function connectServer(url) {
    if (es) es.close();
    S.serverUnits = null;
    const base = url.replace(/\/$/, "");
    es = new EventSource(base + "/api/sim/stream");
    setStatus("srcStatus", `connecting to ${base} …`);
    es.onmessage = (m) => {
      const d = JSON.parse(m.data);
      S.serverUnits = d.units;
      setStatus("srcStatus", `live from sim_server.py · ${d.units.length} units · ${d.clients} HUD client(s) connected`, "ok");
    };
    es.onerror = () => setStatus("srcStatus", "cannot reach sim_server.py (python tools/sim_server.py)", "bad");
  }
  function setStatus(id, txt, cls) {
    const el = $(id);
    if (!el) return;
    el.textContent = txt;
    el.className = "status " + (cls || "");
  }

  // ================================================================ live view
  const L = (App.live = { kind: null, state: null, lastRx: 0, port: null, writer: null, frameTimer: null, stateTimer: null });
  const liveGfx = new H.Gfx(240, 240);

  function logLive(line) {
    const el = $("liveLog");
    if (!el) return;
    el.textContent += line + "\n";
    if (el.textContent.length > 20000) el.textContent = el.textContent.slice(-15000);
    el.scrollTop = el.scrollHeight;
  }

  function onLiveState(st) {
    L.state = st;
    L.lastRx = performance.now();
    const proj = H.projCfg(240, 240, st.fov ? st.fov[0] : 40, st.fov ? st.fov[1] : 40);
    if (st.bore) { proj.cx += st.bore[0]; proj.cy += st.bore[1]; }
    const q = st.att.q || H.fromEuler(st.att.h, st.att.p, st.att.r);
    const targets = (st.targets || []).map((t) => ({ enu: t.e, affil: t.a, dim: t.d, callsign: t.cs, age: t.age }));
    H.render(liveGfx, {
      mode: st.mode || 0, q, proj, ownValid: !!(st.own && st.own.ok), posSource: st.own ? st.own.src : "-",
      hdgSource: st.att.src || "-", link: st.link || 0, maxRange: 5000, radarRange: 2000, maxLabels: 6, thermalMode: 0,
      statusLines: [`IP ${st.ip || "-"}`, `TRACKS ${st.tracks}`, `OWN ${st.own ? st.own.src : "-"}`],
    }, targets);
    drawHud($("liveCanvas"), liveGfx, false);
    $("liveAtt").textContent = `HDG ${st.att.h.toFixed(1)}°  PITCH ${st.att.p.toFixed(1)}°  ROLL ${st.att.r.toFixed(1)}°  · ${st.att.src || ""}`;
    $("liveOwn").textContent = st.own && st.own.ok ? `${st.own.src} ${st.own.lat.toFixed(6)}, ${st.own.lon.toFixed(6)}, ${st.own.hae.toFixed(0)} m` : "no own position";
    $("liveTracks").textContent = `${st.tracks} tracked · link ${["none", "Wi-Fi", "Wi-Fi + TAK"][st.link] || "-"}`;
    App.liveQuat = q;
  }

  async function connectSerial() {
    if (!("serial" in navigator)) {
      setStatus("liveStatus", "Web Serial needs Chrome or Edge (and the page from file:// or http://localhost).", "bad");
      return;
    }
    await disconnectLive();
    try {
      const port = await navigator.serial.requestPort();
      await port.open({ baudRate: 2000000 }); // HUD USB link (firmware/main/usb_link.h)
      L.kind = "serial";
      L.port = port;
      const enc = new TextEncoderStream();
      enc.readable.pipeTo(port.writable);
      L.writer = enc.writable.getWriter();
      setStatus("liveStatus", "USB serial connected — streaming (close idf.py monitor if it is open)", "ok");
      await sendSerial("");
      await sendSerial("stream on 5");
      const dec = new TextDecoderStream();
      port.readable.pipeTo(dec.writable).catch(() => {});
      const reader = dec.readable.getReader();
      L.reader = reader;
      let buf = "";
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        buf += value;
        let i;
        while ((i = buf.indexOf("\n")) >= 0) {
          const line = buf.slice(0, i).replace(/\r$/, "");
          buf = buf.slice(i + 1);
          const k = line.indexOf("@HUD ");
          if (k >= 0) {
            try { onLiveState(JSON.parse(line.slice(k + 5))); } catch (e) { /* partial line */ }
          } else if (line.trim()) logLive(line);
        }
      }
    } catch (e) {
      setStatus("liveStatus", "Serial: " + e.message, "bad");
    }
  }
  async function sendSerial(cmd) {
    if (L.kind === "serial" && L.writer) await L.writer.write(cmd + "\r\n");
    else if (L.kind === "wifi") {
      const m = cmd.match(/^mode (\d)/);
      if (m) fetch(`http://${L.ip}/api/mode?m=${m[1]}`).catch(() => {});
      else if (cmd === "thermal") fetch(`http://${L.ip}/api/thermal`).catch(() => {});
      else logLive("(over Wi-Fi only mode/thermal are available; use USB for the full console)");
    } else if (L.kind === "demo") {
      const m = cmd.match(/^mode (\d)/);
      if (m) S.mode = +m[1];
      if (cmd === "thermal") S.thermal = (S.thermal + 1) % 3;
    }
    if (cmd) logLive("> " + cmd);
  }

  function connectWifi(ip) {
    disconnectLive();
    L.kind = "wifi";
    L.ip = ip.trim();
    setStatus("liveStatus", `polling http://${L.ip}/api/state …`);
    const fctx = $("liveFrame").getContext("2d");
    const img = fctx.createImageData(240, 240);
    L.stateTimer = setInterval(async () => {
      try {
        const r = await fetch(`http://${L.ip}/api/state`, { cache: "no-store" });
        onLiveState(await r.json());
        setStatus("liveStatus", `Wi-Fi connected to ${L.ip}`, "ok");
      } catch (e) {
        setStatus("liveStatus", `cannot reach http://${L.ip}/api/state`, "bad");
      }
    }, 200);
    L.frameTimer = setInterval(async () => {
      try {
        const r = await fetch(`http://${L.ip}/api/frame`, { cache: "no-store" });
        const b = new Uint8Array(await r.arrayBuffer());
        for (let i = 0, j = 0; i < 240 * 240; i++, j += 2) {
          const v = (b[j] << 8) | b[j + 1]; // big-endian RGB565
          img.data[i * 4] = (v >> 11) << 3;
          img.data[i * 4 + 1] = ((v >> 5) & 63) << 2;
          img.data[i * 4 + 2] = (v & 31) << 3;
          img.data[i * 4 + 3] = 255;
        }
        fctx.putImageData(img, 0, 0);
      } catch (e) { /* state poll reports errors */ }
    }, 400);
  }

  function startDemo() {
    disconnectLive();
    L.kind = "demo";
    setStatus("liveStatus", "Demo: streaming the built-in simulator through the same JSON format the HUD sends", "ok");
    const fctx = $("liveFrame").getContext("2d");
    L.stateTimer = setInterval(() => {
      const w = S.world;
      if (!w) return;
      const e = H.toEuler(w.qHud);
      onLiveState({
        t: Date.now(), att: { h: e.h, p: e.p, r: e.r, q: w.qHud, src: "SIM" },
        own: { ok: 1, src: "SIM", lat: w.ownHud[0], lon: w.ownHud[1], hae: w.ownHud[2] },
        link: 2, mode: S.mode, thermal: S.thermal, fov: [S.hfov, S.vfov], bore: [0, 0], ip: "demo", tracks: w.targets.length,
        targets: w.targets.map((t) => ({ uid: t.uid, cs: t.callsign, a: t.affil, d: t.dim, e: t.enu, age: 1 })),
      });
      fctx.imageSmoothingEnabled = false;
      fctx.drawImage(hudCanvas(), 0, 0, 240, 240);
    }, 200);
  }

  async function disconnectLive() {
    clearInterval(L.stateTimer);
    clearInterval(L.frameTimer);
    if (L.kind === "serial") {
      try { await sendSerial("stream off"); } catch (e) { /* ignore */ }
      try { await L.reader.cancel(); } catch (e) { /* ignore */ }
      try { await L.writer.close(); } catch (e) { /* ignore */ }
      try { await L.port.close(); } catch (e) { /* ignore */ }
    }
    L.kind = null;
    setStatus("liveStatus", "not connected");
  }

  // ================================================================ PCB layout
  function initPcb() {
    const P = window.PCB, host = $("pcbView");
    if (!P || !host) return;
    const select = (ref) => {
      P.select(host, ref);
      $("pcbInfo").innerHTML = P.info(ref);
      document.querySelectorAll("#pcbRows tr").forEach((tr) => tr.classList.toggle("sel", tr.dataset.ref === ref));
      App.pcbSelected = ref; // scene3d highlights the same part
    };
    P.renderSvg(host, { onSelect: select });
    $("pcbRows").innerHTML = P.parts
      .map((p) => `<tr data-ref="${p.ref}"><td><span class="swatch" style="background:${P.groups[p.grp].color}"></span> ${p.ref}</td><td>${p.name}</td><td>${p.part}</td></tr>`)
      .join("");
    $("pcbRows").addEventListener("click", (e) => { const tr = e.target.closest("tr"); if (tr) select(tr.dataset.ref); });
    $("pcbLegend").innerHTML =
      Object.values(P.groups).map((g) => `<div class="legend-row"><span class="swatch" style="background:${g.color}"></span>${g.label}</div>`).join("") +
      Object.values(P.nets).map((n) => `<div class="legend-row"><span class="swline" style="background:${n.color}"></span>${n.label}</div>`).join("") +
      `<div class="legend-row"><span class="swatch" style="background:rgba(248,113,113,.3);border:1px dashed #f87171"></span>Keep-out zone</div>`;
    document.querySelectorAll("[data-layer]").forEach((c) => c.addEventListener("change", () => P.setLayer(host, c.dataset.layer, c.checked)));
    App.pcbSelect = select;
  }

  // ================================================================ UI
  function bindRange(id, obj, key, fmtFn) {
    const el = $(id), out = $(id + "Val");
    const upd = () => { obj[key] = parseFloat(el.value); if (out) out.textContent = fmtFn ? fmtFn(obj[key]) : el.value; };
    el.addEventListener("input", upd);
    upd();
  }

  function init() {
    // Tabs
    document.querySelectorAll("nav button[data-tab]").forEach((b) =>
      b.addEventListener("click", () => {
        document.querySelectorAll("nav button").forEach((x) => x.classList.toggle("active", x === b));
        document.querySelectorAll("section.tab").forEach((s) => s.classList.toggle("active", s.id === b.dataset.tab));
        window.dispatchEvent(new Event("resize"));
        try { localStorage.setItem("hud.tab", b.dataset.tab); } catch (e) { /* ignore */ }
      })
    );
    let saved = null;
    try { saved = localStorage.getItem("hud.tab"); } catch (e) { /* ignore */ }
    const start = document.querySelector(`nav button[data-tab="${saved}"]`) || document.querySelector("nav button[data-tab]");
    start.click();

    bindRange("hdg", S.att, "h", (v) => v.toFixed(0) + "°");
    bindRange("pitch", S.att, "p", (v) => v.toFixed(0) + "°");
    bindRange("roll", S.att, "r", (v) => v.toFixed(0) + "°");
    bindRange("hfov", S, "hfov", (v) => v + "°");
    bindRange("vfov", S, "vfov", (v) => v + "°");
    bindRange("hdgErr", S, "hdgErr", (v) => v.toFixed(1) + "°");
    bindRange("posErr", S, "posErr", (v) => v.toFixed(0) + " m");
    $("scan").addEventListener("change", (e) => (S.scan = e.target.checked));
    document.querySelectorAll("[data-mode]").forEach((b) =>
      b.addEventListener("click", () => {
        S.mode = +b.dataset.mode;
        document.querySelectorAll("[data-mode]").forEach((x) => x.classList.toggle("on", x === b));
      })
    );
    document.querySelectorAll("[data-thermal]").forEach((b) =>
      b.addEventListener("click", () => {
        S.thermal = +b.dataset.thermal;
        document.querySelectorAll("[data-thermal]").forEach((x) => x.classList.toggle("on", x === b));
      })
    );
    $("srcBuiltin").addEventListener("click", () => {
      S.source = "builtin";
      if (es) es.close();
      setStatus("srcStatus", "built-in scenario (same as the firmware's fake mode)");
    });
    $("srcServer").addEventListener("click", () => {
      S.source = "server";
      const def = location.protocol.startsWith("http") ? location.origin : "http://localhost:8000";
      connectServer($("serverUrl").value || def);
    });
    if (location.protocol.startsWith("http")) $("serverUrl").value = location.origin;
    $("targetRows").addEventListener("click", (e) => {
      const tr = e.target.closest("tr");
      if (tr) S.selected = tr.dataset.uid;
    });
    $("liveUsb").addEventListener("click", connectSerial);
    $("liveWifi").addEventListener("click", () => connectWifi($("liveIp").value));
    $("liveDemo").addEventListener("click", startDemo);
    $("liveStop").addEventListener("click", disconnectLive);
    $("liveSend").addEventListener("submit", (e) => {
      e.preventDefault();
      const i = $("liveCmd");
      sendSerial(i.value);
      i.value = "";
    });
    document.querySelectorAll("[data-livecmd]").forEach((b) => b.addEventListener("click", () => sendSerial(b.dataset.livecmd)));
    try { const ip = localStorage.getItem("hud.ip"); if (ip) $("liveIp").value = ip; } catch (e) { /* ignore */ }
    $("liveIp").addEventListener("change", (e) => { try { localStorage.setItem("hud.ip", e.target.value); } catch (x) { /* ignore */ } });

    initPcb();
    setStatus("srcStatus", "built-in scenario (same as the firmware's fake mode)");
    setStatus("liveStatus", "not connected");
    const loop = () => { step(); requestAnimationFrame(loop); };
    loop();
  }

  document.addEventListener("DOMContentLoaded", init);
})();
