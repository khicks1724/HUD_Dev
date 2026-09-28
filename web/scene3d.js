/*
 * scene3d.js - three.js views. Classic script; index.html's module loads
 * three from the CDN and calls Scene3D.init(THREE, addons).
 *
 * Frame convention: ENU (east, north, up) -> three (x, y, z) = (e, u, -n).
 * HUD body axes (right, forward, up) map the same way, so a body->world
 * quaternion q = (w, x, y, z) becomes (w, x, z, -y) in three.
 */
(function () {
  "use strict";
  const Scene3D = (window.Scene3D = {});

  Scene3D.init = function (THREE, addons) {
    const H = window.HudMath, App = window.App, S = App.state;
    const { OrbitControls, CSS2DRenderer, CSS2DObject } = addons;
    const $ = (id) => document.getElementById(id);
    const V = (e, n, u) => new THREE.Vector3(e, u, -n);
    const Q = (q) => new THREE.Quaternion(q[1], q[3], -q[2], q[0]);
    const AFF_COL = [0xffe63c, 0x38bdf8, 0xff6b6b, 0x8cff78]; // RF SIM friendly/enemy hues

    function makeRenderer(host) {
      const r = new THREE.WebGLRenderer({ antialias: true, alpha: false });
      r.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
      host.appendChild(r.domElement);
      return r;
    }
    function fit(renderer, host, camera, aspectLock) {
      const w = host.clientWidth, h = host.clientHeight;
      if (!w || !h) return false;
      const c = renderer.domElement;
      if (c.width !== Math.floor(w * renderer.getPixelRatio()) || c.height !== Math.floor(h * renderer.getPixelRatio())) {
        renderer.setSize(w, h, false);
        if (!aspectLock) { camera.aspect = w / h; camera.updateProjectionMatrix(); }
      }
      return true;
    }
    const visible = (el) => el && el.offsetParent !== null;

    // ============================================================ world (simulator)
    const worldHost = $("worldView"), eyeHost = $("eyeView");
    const world = new THREE.Scene();
    world.background = new THREE.Color(0x020202);
    world.fog = new THREE.Fog(0x020202, 3000, 9000);
    world.add(new THREE.HemisphereLight(0xbfd9ff, 0x223322, 1.1));
    const sun = new THREE.DirectionalLight(0xffffff, 1.2);
    sun.position.set(2000, 3000, 1000);
    world.add(sun);

    const ground = new THREE.Mesh(new THREE.PlaneGeometry(12000, 12000), new THREE.MeshLambertMaterial({ color: 0x111316 }));
    ground.rotation.x = -Math.PI / 2;
    world.add(ground);
    const grid = new THREE.GridHelper(8000, 80, 0x5a5a5a, 0x262626);
    grid.position.y = 0.2;
    world.add(grid);

    const worldCam = new THREE.PerspectiveCamera(50, 1, 1, 30000);
    worldCam.position.set(-1400, 1500, 1600);
    const worldR = makeRenderer(worldHost);
    const labelR = new CSS2DRenderer();
    labelR.domElement.className = "labels";
    worldHost.appendChild(labelR.domElement);
    const controls = new OrbitControls(worldCam, labelR.domElement);
    controls.target.set(0, 0, -600);
    controls.maxPolarAngle = Math.PI * 0.49;
    controls.update();

    function label(text, cls) {
      const d = document.createElement("div");
      d.className = "lbl " + (cls || "");
      d.textContent = text;
      return new CSS2DObject(d);
    }

    // North marker and range rings (follow own position)
    const north = label("N", "north");
    north.position.set(0, 5, -2600);
    world.add(north);
    const rings = new THREE.Group();
    [500, 1000, 2000].forEach((r) => {
      const g = new THREE.RingGeometry(r - 2, r + 2, 128);
      const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color: 0x8a8a8a, side: THREE.DoubleSide, transparent: true, opacity: 0.8 }));
      m.rotation.x = -Math.PI / 2;
      m.position.y = 0.5;
      rings.add(m);
      const l = label(r >= 1000 ? r / 1000 + " km" : r + " m", "ring");
      l.position.set(0, 1, -r);
      rings.add(l);
    });
    world.add(rings);

    // Units
    const unitObjs = new Map();
    function unitMesh(u) {
      const a = H.affilFromType(u.type), d = H.dimFromType(u.type);
      const mat = new THREE.MeshStandardMaterial({ color: AFF_COL[a], emissive: AFF_COL[a], emissiveIntensity: 0.35 });
      let geo;
      if (d === H.DIM.AIR) geo = new THREE.ConeGeometry(14, 40, 4);
      else if (a === H.AFFIL.HOSTILE) geo = new THREE.OctahedronGeometry(16);
      else if (a === H.AFFIL.FRIEND) geo = new THREE.BoxGeometry(24, 24, 24);
      else if (a === H.AFFIL.NEUTRAL) geo = new THREE.BoxGeometry(20, 20, 20);
      else geo = new THREE.SphereGeometry(13, 16, 12);
      const g = new THREE.Group();
      const m = new THREE.Mesh(geo, mat);
      if (d === H.DIM.AIR) m.rotation.x = -Math.PI / 2;
      m.position.y = 14;
      g.add(m);
      const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.8, 0.8, 1, 6), new THREE.MeshBasicMaterial({ color: AFF_COL[a] }));
      g.add(pole);
      const lab = label(u.cs, "a" + a);
      lab.position.y = 45;
      g.add(lab);
      g.userData = { pole, mesh: m };
      world.add(g);
      return g;
    }

    // Own marker + frustums
    const own = new THREE.Group();
    own.add(new THREE.Mesh(new THREE.CylinderGeometry(6, 6, 18, 16), new THREE.MeshStandardMaterial({ color: 0xffffff })));
    const ownLab = label("YOU (HUD)", "own");
    ownLab.position.y = 40;
    own.add(ownLab);
    world.add(own);

    function frustum(color, len) {
      const geo = new THREE.BufferGeometry();
      geo.setAttribute("position", new THREE.Float32BufferAttribute(new Array(16 * 3).fill(0), 3));
      const line = new THREE.LineSegments(geo, new THREE.LineBasicMaterial({ color, transparent: true, opacity: 0.9 }));
      line.userData.len = len;
      world.add(line);
      return line;
    }
    const fTrue = frustum(0x34d399, 600), fHud = frustum(0xf7b955, 600);
    function setFrustum(line, pos, q, hfov, vfov) {
      const L = line.userData.len, th = Math.tan((hfov / 2) * H.D2R), tv = Math.tan((vfov / 2) * H.D2R);
      const corners = [[-th, tv], [th, tv], [th, -tv], [-th, -tv]].map(([x, z]) => {
        const b = H.qrot(q, [x * L, L, z * L]);
        return V(pos[0] + b[0], pos[1] + b[1], pos[2] + b[2]);
      });
      const o = V(pos[0], pos[1], pos[2]);
      const pts = [];
      corners.forEach((c, i) => { pts.push(o, c, c, corners[(i + 1) % 4]); });
      line.geometry.setFromPoints(pts);
    }

    const uavPath = new THREE.Mesh(new THREE.TorusGeometry(600, 1.5, 6, 128), new THREE.MeshBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0.35 }));
    uavPath.rotation.x = Math.PI / 2;
    world.add(uavPath);

    // Eye view: camera at the observer, looking through the HUD
    const eyeCam = new THREE.PerspectiveCamera(40, 1, 0.5, 30000);
    const eyeR = makeRenderer(eyeHost);
    const eyeHud = $("eyeHud");

    function updateWorld() {
      const w = S.world;
      if (!w) return;
      const o = S.origin;
      const seen = new Set();
      for (const u of w.units) {
        if (u.uid === "ANDROID-SIM-OWNER") continue;
        seen.add(u.uid);
        let g = unitObjs.get(u.uid);
        if (!g) { g = unitMesh(u); unitObjs.set(u.uid, g); }
        const e = H.llaToEnu(o, [u.lat, u.lon, u.hae]);
        g.position.copy(V(e[0], e[1], 0));
        const hgt = Math.max(0.5, e[2]);
        g.userData.pole.scale.y = hgt;
        g.userData.pole.position.y = hgt / 2;
        g.userData.mesh.position.y = e[2] + 14;
        g.children[2].position.y = e[2] + 45;
        g.userData.mesh.rotation.y = (-u.course * Math.PI) / 180;
        g.userData.mesh.scale.setScalar(u.uid === S.selected ? 1.6 : 1);
      }
      for (const [uid, g] of unitObjs) if (!seen.has(uid)) { world.remove(g); unitObjs.delete(uid); }

      const ownE = H.llaToEnu(o, w.ownTrue);
      own.position.copy(V(ownE[0], ownE[1], 0));
      own.children[0].position.y = 9;
      rings.position.set(own.position.x, 0, own.position.z);
      const uavC = H.llaToEnu(o, H.enuOffsetToLla(o, 1100, 1100, 450));
      uavPath.position.copy(V(uavC[0], uavC[1], 450));

      setFrustum(fTrue, ownE, w.qTrue, S.hfov, S.vfov);
      const diverged = Math.abs(S.hdgErr) > 0.05 || S.posErr > 0.5;
      fHud.visible = diverged;
      if (diverged) {
        const hudE = H.llaToEnu(o, w.ownHud);
        setFrustum(fHud, hudE, w.qHud, S.hfov, S.vfov);
      }

      // Eye camera = true pose. The HUD overlay uses the HUD's believed pose,
      // so any heading/GPS error shows up as misregistration.
      eyeCam.position.copy(V(ownE[0], ownE[1], ownE[2]));
      eyeCam.quaternion.copy(Q(w.qTrue));
      eyeCam.fov = S.vfov;
      eyeCam.aspect = Math.tan((S.hfov / 2) * H.D2R) / Math.tan((S.vfov / 2) * H.D2R);
      eyeCam.updateProjectionMatrix();
    }

    // ============================================================ hardware viewer
    // Light "product shot" stage: soft shadows, outlined edges, optional labels.
    const hwHost = $("hwView");
    const hw = new THREE.Scene();
    hw.background = new THREE.Color(0xe3e5e8);
    hw.add(new THREE.HemisphereLight(0xffffff, 0x8a8f96, 1.15));
    hw.add(new THREE.AmbientLight(0xffffff, 0.25));
    const key = new THREE.DirectionalLight(0xffffff, 2.3);
    key.position.set(2.5, 5, 3);
    key.castShadow = true;
    key.shadow.mapSize.set(2048, 2048);
    Object.assign(key.shadow.camera, { left: -3, right: 3, top: 3, bottom: -3, near: 0.1, far: 20 });
    key.shadow.bias = -0.0005;
    hw.add(key);
    const fill = new THREE.DirectionalLight(0xdfe8ff, 0.9);
    fill.position.set(-3, 2, -2);
    hw.add(fill);
    const floor = new THREE.Mesh(new THREE.PlaneGeometry(20, 20), new THREE.ShadowMaterial({ opacity: 0.16 }));
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    hw.add(floor);
    const floorGrid = new THREE.GridHelper(8, 80, 0xc4c8cd, 0xd3d6da);
    floorGrid.position.y = 0.0005;
    hw.add(floorGrid);

    const hwCam = new THREE.PerspectiveCamera(35, 1, 0.01, 100);
    const hwR = makeRenderer(hwHost);
    hwR.shadowMap.enabled = true;
    hwR.shadowMap.type = THREE.PCFSoftShadowMap;
    hwR.toneMapping = THREE.ACESFilmicToneMapping;
    hwR.toneMappingExposure = 1.05;
    const hwLabelR = new CSS2DRenderer();
    hwLabelR.domElement.className = "labels hwlabels";
    hwHost.appendChild(hwLabelR.domElement);
    const hwCtl = new OrbitControls(hwCam, hwLabelR.domElement);
    hwCtl.enableDamping = true;

    const mm = 0.01; // 1 mm = 0.01 units
    const parts = [];
    const edgeMat = new THREE.LineBasicMaterial({ color: 0x1b1d20, transparent: true, opacity: 0.55 });
    function part(group, name, desc, geo, color, pos, explode, opts, extra) {
      const mat = new THREE.MeshStandardMaterial(Object.assign({ color, roughness: 0.55, metalness: 0.1 }, opts || {}));
      const m = new THREE.Mesh(geo, mat);
      m.position.set(pos[0] * mm, pos[1] * mm, pos[2] * mm);
      m.castShadow = !mat.transparent;
      m.receiveShadow = true;
      m.add(new THREE.LineSegments(new THREE.EdgesGeometry(geo, 25), edgeMat));
      m.userData = Object.assign({ name, desc, base: m.position.clone(), explode: new THREE.Vector3(...(explode || [0, 0, 0])).multiplyScalar(mm) }, extra || {});
      if (extra && extra.label) {
        const l = label(extra.label, "");
        geo.computeBoundingBox();
        l.position.set(0, geo.boundingBox.max.y + 0.03, 0);
        m.add(l);
        m.userData.labelObj = l;
      }
      group.add(m);
      parts.push(m);
      return m;
    }
    const B = (x, y, z) => new THREE.BoxGeometry(x * mm, y * mm, z * mm);
    const CYL = (r, len, seg) => new THREE.CylinderGeometry(r * mm, r * mm, len * mm, seg || 32);
    const MAT = {
      pcb: { color: 0x1d6b3e, roughness: 0.45 }, chip: 0x25282c, metal: { metalness: 0.85, roughness: 0.3 },
      glass: { transparent: true, opacity: 0.3, roughness: 0.05, metalness: 0.1, depthWrite: false },
    };

    // ---------------------------------------------------------- desk build (V1 + V2)
    const desk = new THREE.Group();
    part(desk, "Waveshare ESP32-S3-LCD-1.3-C", "ESP32-S3R8 (8 MB PSRAM, 16 MB flash), Wi-Fi/BLE, QMI8658 IMU, CH343 USB-C, LiPo charger. Runs the HUD firmware.",
      B(40, 12, 40), 0x3a3f46, [0, 26, 0], [0, 20, 0], null, { label: "Waveshare S3 (HUD)" });
    part(desk, "ST7789 1.3\" 240×240 LCD", "SPI display on GPIO38-42, backlight PWM on GPIO20. Drawn mirrored so the prism reflection reads correctly.",
      B(27, 1.2, 27), 0x0a0a0a, [0, 32.6, 0], [0, 32, 0], { roughness: 0.15 }, { label: "LCD" });
    part(desk, "Prism (beam splitter cube)", "Reflects the LCD towards your eye while you see through it. Uncollimated: the image sits at the cube, not at infinity.",
      B(25, 25, 25), 0x8fd3ff, [0, 45.8, 0], [0, 60, 0], MAT.glass, { label: "Prism" });
    part(desk, "Backpack PCB (V2)", "Plugs onto the GPIO headers: GNSS, magnetometer, fuel gauge, encoder, haptic. hardware/backpack/.",
      B(40, 1.6, 40), 0x1d6b3e, [0, 10.8, 0], [0, 0, 0], MAT.pcb, { label: "V2 backpack" });
    part(desk, "u-blox SAM-M10Q GNSS", "Own position + time. Integrated patch antenna: point it at the sky.",
      B(15.5, 6.3, 15.5), 0xd4a93c, [-8, 14.8, -8], [0, 4, 0], MAT.metal, { label: "GNSS" });
    part(desk, "MMC5983MA magnetometer", "Absolute heading. Placed at the far corner from the battery and motor.",
      B(3, 1, 3), MAT.chip, [15, 12.1, 15], [4, 4, 4], null, { label: "Mag" });
    part(desk, "1S LiPo 1200-2000 mAh", "Plugs into the Waveshare MX1.25 BAT connector; ~7 h HUD-only.",
      B(50, 8, 34), 0xc9ccd1, [0, 4, 0], [0, -25, 0], { metalness: 0.6, roughness: 0.35 }, { label: "LiPo" });
    hw.add(desk);

    // ---------------------------------------------------------- rail build (V3 + Boson)
    const rail = new THREE.Group();
    const railBar = part(rail, "Picatinny rail (MIL-STD-1913)", "21.2 mm wide, slots 5.23 mm on a 10.01 mm pitch. M-LOK handguards take a short M-LOK-to-Picatinny section.",
      B(260, 8, 21.2), 0x3c3f43, [0, 4, 0], [0, -10, 0], { metalness: 0.6, roughness: 0.4 }, { label: "Picatinny rail" });
    for (let i = -12; i <= 12; i++) {
      const s = new THREE.Mesh(B(5.23, 3, 22), new THREE.MeshStandardMaterial({ color: 0x15171a }));
      s.position.set(i * 10.01 * mm, 3 * mm, 0);
      railBar.add(s);
    }
    part(rail, "QD Picatinny clamp", "Lever clamp with recoil lug: repeatable return-to-zero, so HUD and thermal boresight survive remounting.",
      B(40, 12, 34), 0x55595e, [0, 14, 0], [0, 10, 0], { metalness: 0.5 }, { label: "QD clamp" });
    part(rail, "Housing (concept)", "Rigid chassis ties Boson, IMU and eyepiece together. hardware/mount/hud_rail_mount.scad.",
      B(150, 46, 44), 0x9aa3ab, [0, 43, 0], [0, 60, 0], { transparent: true, opacity: 0.22, depthWrite: false }, { label: "Housing" });
    part(rail, "FLIR Boson 640 (21640AS50)", "640×512 LWIR, 50° HFOV, 60 Hz, 1.8 V CMOS or USB via VPC. 500-1550 mW.",
      B(21, 21, 21), 0x7d8288, [55, 48, 0], [60, 60, 0], { metalness: 0.5 }, { label: "Boson 640" });
    const lens = part(rail, "Boson lens (50° HFOV)", "Short lens; the HUD crops the centre ~499 px to match a 40° display.",
      new THREE.CylinderGeometry(8 * mm, 9 * mm, 18 * mm, 32), 0x202225, [74, 48, 0], [85, 60, 0], { metalness: 0.4 });
    lens.rotation.z = Math.PI / 2;
    part(rail, "Boson USB VPC (421-0061-00)", "USB-C video/power/control. Bench + configuration; direct UVC input on an ESP32-P4.",
      B(21, 4, 21), 0x1d6b3e, [36, 48, 0], [40, 60, 0], MAT.pcb, { label: "VPC" });
    part(rail, "HUD core board (V3)", "ESP32-P4 + C6 recommended with thermal (USB-HS UVC, PPA blending); ESP32-S3 without.",
      B(45, 2, 32), 0x1d6b3e, [-10, 36, 0], [0, 70, 0], MAT.pcb, { label: "Core board" });
    part(rail, "LiPo 2000 mAh", "Low centre of gravity; ~4-5 h with the Boson.", B(70, 10, 36), 0xc9ccd1, [-5, 26, 0], [0, 30, 0], { metalness: 0.6 }, { label: "LiPo" });
    const eyep = part(rail, "Micro-display + eyepiece", "Collimated view at rail eye relief (the Waveshare prism is a desk demo).",
      new THREE.CylinderGeometry(13 * mm, 13 * mm, 30 * mm, 32), 0x2a2c30, [-90, 48, 0], [-110, 60, 0], null, { label: "Eyepiece" });
    eyep.rotation.z = Math.PI / 2;
    hw.add(rail);

    // ---------------------------------------------------------- hub build (V2H), from pcb.js
    const hub = new THREE.Group();
    const P = window.PCB;
    const BOARD_Y = 20; // board underside above the floor (cells below)
    const toX = (x) => x - P.board.w / 2, toZ = (y) => y - P.board.h / 2;
    part(hub, "Hub backpack PCB (V2H)", `${P.board.w}×${P.board.h} mm, ${P.board.layers} layers. Charger, 5 V boost, 3.3 V, three USB-C ports, header link to the HUD. See the PCB Layout tab.`,
      B(P.board.w, P.board.thickness, P.board.h), 0x1d6b3e, [0, BOARD_Y + P.board.thickness / 2, 0], [0, 0, 0], MAT.pcb, { label: "V2H hub PCB" });
    const top = BOARD_Y + P.board.thickness;
    const kindColor = { ic: 0x25282c, module: 0xb9bdc3, passive: 0x4a4d52, mech: 0x2a2c30, conn: 0x141516, usb: 0xc7cbd1 };
    for (const p of P.parts) {
      let color = kindColor[p.kind], opts = null;
      if (p.ref === "U9") { color = 0xd4a93c; opts = MAT.metal; }
      else if (p.kind === "usb" || p.kind === "module") opts = MAT.metal;
      const explodeUp = p.kind === "conn" && p.ref.startsWith("H") ? 10 : 6;
      part(hub, `${p.ref} · ${p.name}`, `${p.part}. ${p.why}`, B(p.w, p.z, p.h), color,
        [toX(p.x), top + p.z / 2, toZ(p.y)], [0, explodeUp, 0], opts,
        { ref: p.ref, label: p.w * p.h > 30 || p.kind === "usb" ? p.ref : null });
    }
    const hd = P.head, headY = top + hd.stack;
    const hx = toX(hd.x + hd.w / 2), hz = toZ(hd.y + hd.h / 2);
    part(hub, "Waveshare HUD head (ESP32-S3)", "Display, IMU, Wi-Fi/TAK and renderer. Powered with 5 V from the hub through its USB VCC header pin.",
      B(40, 12, 40), 0x3a3f46, [hx, headY + 6, hz], [0, 45, 0], null, { label: "HUD head (S3)" });
    part(hub, "Prism", "Beam splitter over the LCD.", B(25, 25, 25), 0x8fd3ff, [hx, headY + 12 + 12.5, hz], [0, 70, 0], MAT.glass, { label: "Prism" });
    [-9.5, 9.5].forEach((z, i) => {
      const c = part(hub, "18650 Li-ion cell " + (i + 1), "1S2P pack, 6-7 Ah (≈25 Wh): ≈7-8 h with thermal on.",
        CYL(9, 65, 32), 0x2f6fd0, [0, 9.5, z], [0, -12, z * 0.8], { roughness: 0.35 }, i ? null : { label: "1S2P 18650 pack" });
      c.rotation.z = Math.PI / 2;
    });
    // camera and phone off the USB-C edge (board bottom edge = +Z)
    const edgeZ = toZ(P.board.h);
    part(hub, "FLIR Boson 640 + lens", "Powered and read over USB by the hub (VPC now, direct 80-pin connector later).",
      B(21, 21, 21), 0x7d8288, [toX(52) - 20, 10.5, edgeZ + 45], [-20, 0, 30], { metalness: 0.5 }, { label: "Boson 640 + VPC" });
    part(hub, "Boson USB VPC (421-0061-00)", "USB-C video/power/control board behind the camera.", B(21, 4, 21), 0x1d6b3e, [toX(52) - 20, 23, edgeZ + 45], [-20, 4, 30], MAT.pcb);
    part(hub, "ATAK phone", "USB-C to the PHONE port. The plugin sends tracks and its GPS; the HUD sends back attitude.",
      B(75, 8, 160), 0x1b1d21, [-P.board.w / 2 - 50, 4, -20], [-30, 0, -20], { roughness: 0.3 }, { label: "ATAK phone", noFit: true });
    function cable(a, b) {
      const pts = [a, new THREE.Vector3((a.x + b.x) / 2, Math.min(a.y, b.y) - 6, (a.z + b.z) / 2), b].map((v) => v.clone().multiplyScalar(mm));
      const m = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 32, 1.6 * mm, 10), new THREE.MeshStandardMaterial({ color: 0x2b2d31, roughness: 0.6 }));
      m.castShadow = true;
      hub.add(m);
    }
    cable(new THREE.Vector3(toX(52), top + 1.6, edgeZ + 2), new THREE.Vector3(toX(52) - 20, 23, edgeZ + 34));
    cable(new THREE.Vector3(toX(64), top + 1.6, edgeZ + 2), new THREE.Vector3(-P.board.w / 2 - 12, 8, 30));
    hw.add(hub);

    // ---------------------------------------------------------- build switch, fit, pick
    let explode = 0, current = "hub";
    const groups = { desk, rail, hub };
    function fitView(which) {
      const box = new THREE.Box3();
      groups[which].traverse((o) => { if (o.isMesh && !o.userData.noFit && !(o.parent && o.parent.userData.noFit)) box.expandByObject(o); });
      const c = box.getCenter(new THREE.Vector3()), size = box.getSize(new THREE.Vector3());
      const r = Math.max(size.x, size.y * 1.4, size.z) * 0.5;
      const dist = (r / Math.tan((hwCam.fov * Math.PI) / 360)) * (which === "rail" ? 0.95 : 1.15);
      const dir = (which === "rail" ? new THREE.Vector3(0.3, 0.45, 1) : new THREE.Vector3(1, 0.85, 1.15)).normalize();
      hwCam.position.copy(c).addScaledVector(dir, dist);
      hwCtl.target.copy(c);
      hwCtl.update();
    }
    function setBuild(which) {
      current = which;
      for (const k in groups) {
        groups[k].visible = k === which;
        // CSS2D labels ignore ancestor visibility, so hide them explicitly
        groups[k].traverse((o) => { if (o.isCSS2DObject) o.visible = k === which; });
      }
      fitView(which);
    }
    document.querySelectorAll("[data-build]").forEach((b) =>
      b.addEventListener("click", () => {
        setBuild(b.dataset.build);
        document.querySelectorAll("[data-build]").forEach((x) => x.classList.toggle("on", x === b));
      })
    );
    $("explode").addEventListener("input", (e) => (explode = parseFloat(e.target.value)));
    // Re-frame when the slider is released so exploded parts stay in view.
    $("explode").addEventListener("change", () => {
      for (const p of parts) p.position.copy(p.userData.base).addScaledVector(p.userData.explode, explode);
      fitView(current);
    });
    const labelsBox = $("hwLabels");
    const applyLabels = () => { hwLabelR.domElement.style.display = labelsBox && !labelsBox.checked ? "none" : ""; };
    if (labelsBox) labelsBox.addEventListener("change", applyLabels);
    $("hwFit").addEventListener("click", () => fitView(current));

    const ray = new THREE.Raycaster(), mouse = new THREE.Vector2();
    let picked = null, lastPcbSel = null;
    function highlight(m) {
      if (picked) picked.material.emissive.setHex(0x000000);
      picked = m;
      if (picked) {
        picked.material.emissive.setHex(0x3a5f9a);
        picked.material.emissiveIntensity = 0.6;
        $("hwInfo").innerHTML = `<b>${picked.userData.name}</b><br>${picked.userData.desc}`;
      }
    }
    let downAt = null;
    hwLabelR.domElement.addEventListener("pointerdown", (ev) => (downAt = [ev.clientX, ev.clientY]));
    hwLabelR.domElement.addEventListener("pointerup", (ev) => {
      if (!downAt || Math.hypot(ev.clientX - downAt[0], ev.clientY - downAt[1]) > 4) return; // it was a drag
      const r = hwLabelR.domElement.getBoundingClientRect();
      mouse.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(mouse, hwCam);
      const hit = ray.intersectObjects(parts.filter((p) => p.parent.visible), false)[0];
      highlight(hit ? hit.object : null);
      if (hit && hit.object.userData.ref && App.pcbSelect) { lastPcbSel = hit.object.userData.ref; App.pcbSelect(hit.object.userData.ref); }
    });
    function syncFromPcb() {
      if (App.pcbSelected && App.pcbSelected !== lastPcbSel) {
        lastPcbSel = App.pcbSelected;
        const m = parts.find((p) => p.userData.ref === App.pcbSelected);
        if (m) highlight(m);
      }
    }
    setBuild("hub");

    // ============================================================ live attitude view
    const attHost = $("liveAttView");
    const att = new THREE.Scene();
    att.background = new THREE.Color(0x020202);
    att.add(new THREE.HemisphereLight(0xffffff, 0x334433, 1.1));
    const attCam = new THREE.PerspectiveCamera(40, 1, 0.01, 50);
    attCam.position.set(1.4, 1.0, 1.6);
    attCam.lookAt(0, 0, 0);
    const attR = makeRenderer(attHost);
    const dev = new THREE.Group();
    dev.add(new THREE.Mesh(B(40, 12, 40), new THREE.MeshStandardMaterial({ color: 0x2a2f36 })));
    const pr = new THREE.Mesh(B(25, 25, 25), new THREE.MeshStandardMaterial({ color: 0x9fd9ff, transparent: true, opacity: 0.35 }));
    pr.position.y = 19 * mm;
    dev.add(pr);
    const fwd = new THREE.ArrowHelper(new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, 0.2, 0), 0.7, 0x34d399, 0.1, 0.06);
    dev.add(fwd);
    att.add(dev);
    att.add(new THREE.AxesHelper(0.9));
    const nLab = new THREE.ArrowHelper(new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, -0.3, 0), 0.9, 0x38bdf8, 0.08, 0.05);
    att.add(nLab);
    att.add(new THREE.GridHelper(2, 8, 0x3a3a3a, 0x1a1a1a));

    // ============================================================ loop
    function frame() {
      requestAnimationFrame(frame);
      if (visible(worldHost) || visible(eyeHost)) updateWorld();
      if (visible(worldHost) && fit(worldR, worldHost, worldCam)) {
        labelR.setSize(worldHost.clientWidth, worldHost.clientHeight);
        controls.update();
        worldR.render(world, worldCam);
        labelR.render(world, worldCam);
      }
      if (visible(eyeHost) && fit(eyeR, eyeHost, eyeCam, true)) {
        // Hide the observer's own marker and frustums when looking from the eye.
        const vis = [own.visible, fTrue.visible, fHud.visible];
        own.visible = fTrue.visible = fHud.visible = false;
        eyeR.render(world, eyeCam);
        [own.visible, fTrue.visible, fHud.visible] = vis;
        const ctx = eyeHud.getContext("2d");
        ctx.imageSmoothingEnabled = false;
        ctx.clearRect(0, 0, eyeHud.width, eyeHud.height);
        ctx.drawImage($("hudCanvas"), 0, 0, eyeHud.width, eyeHud.height);
      }
      if (visible(hwHost) && fit(hwR, hwHost, hwCam)) {
        for (const p of parts) p.position.copy(p.userData.base).addScaledVector(p.userData.explode, explode);
        syncFromPcb();
        hwCtl.update();
        hwR.render(hw, hwCam);
        hwLabelR.setSize(hwHost.clientWidth, hwHost.clientHeight);
        hwLabelR.render(hw, hwCam);
      }
      if (visible(attHost) && fit(attR, attHost, attCam)) {
        if (App.liveQuat) dev.quaternion.copy(Q(App.liveQuat));
        attR.render(att, attCam);
      }
    }
    frame();
    document.body.classList.add("three-ok");
  };
})();
