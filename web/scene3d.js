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
    const AFF_COL = [0xffe63c, 0x50c8ff, 0xff3c3c, 0x8cff78];

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
    world.background = new THREE.Color(0x0b1016);
    world.fog = new THREE.Fog(0x0b1016, 3000, 9000);
    world.add(new THREE.HemisphereLight(0xbfd9ff, 0x223322, 1.1));
    const sun = new THREE.DirectionalLight(0xffffff, 1.2);
    sun.position.set(2000, 3000, 1000);
    world.add(sun);

    const ground = new THREE.Mesh(new THREE.PlaneGeometry(12000, 12000), new THREE.MeshLambertMaterial({ color: 0x1b2a1f }));
    ground.rotation.x = -Math.PI / 2;
    world.add(ground);
    const grid = new THREE.GridHelper(8000, 80, 0x2e4a36, 0x223528);
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
      const m = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color: 0x3f6f4f, side: THREE.DoubleSide, transparent: true, opacity: 0.6 }));
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
    const fTrue = frustum(0x00ff5a, 600), fHud = frustum(0xffaa00, 600);
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

    const uavPath = new THREE.Mesh(new THREE.TorusGeometry(600, 1.5, 6, 128), new THREE.MeshBasicMaterial({ color: 0x50c8ff, transparent: true, opacity: 0.35 }));
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
    const hwHost = $("hwView");
    const hw = new THREE.Scene();
    hw.background = new THREE.Color(0x0d1210);
    hw.add(new THREE.HemisphereLight(0xffffff, 0x445544, 1.8));
    hw.add(new THREE.AmbientLight(0xffffff, 0.35));
    const key = new THREE.DirectionalLight(0xffffff, 1.4);
    key.position.set(3, 5, 4);
    hw.add(key);
    const hwCam = new THREE.PerspectiveCamera(40, 1, 0.01, 100);
    hwCam.position.set(2.2, 1.6, 2.4);
    const hwR = makeRenderer(hwHost);
    const hwCtl = new OrbitControls(hwCam, hwR.domElement);
    hwCtl.target.set(0, 0.2, 0);
    hwCtl.update();

    const mm = 0.01; // 1 mm = 0.01 units
    const parts = [];
    function part(group, name, desc, geo, color, pos, explode, opts) {
      const mat = new THREE.MeshStandardMaterial(Object.assign({ color, roughness: 0.6, metalness: 0.1 }, opts || {}));
      const m = new THREE.Mesh(geo, mat);
      m.position.set(pos[0] * mm, pos[1] * mm, pos[2] * mm);
      m.userData = { name, desc, base: m.position.clone(), explode: new THREE.Vector3(...(explode || [0, 0, 0])).multiplyScalar(mm) };
      group.add(m);
      parts.push(m);
      return m;
    }
    const B = (x, y, z) => new THREE.BoxGeometry(x * mm, y * mm, z * mm);

    // Desk build: Waveshare -C + backpack + LiPo
    const desk = new THREE.Group();
    part(desk, "Waveshare ESP32-S3-LCD-1.3-C", "ESP32-S3R8 (8 MB PSRAM, 16 MB flash), Wi-Fi/BLE, QMI8658 IMU, CH343 USB-C, LiPo charger. Runs the HUD firmware.",
      B(40, 12, 40), 0x2a2f36, [0, 6, 0], [0, 0, 0]);
    part(desk, "ST7789 1.3\" 240×240 LCD", "SPI display on GPIO38-42, backlight PWM on GPIO20. Drawn mirrored so the prism reflection reads correctly.",
      B(27, 1.2, 27), 0x050505, [0, 12.8, 0], [0, 12, 0], { roughness: 0.2 });
    part(desk, "Prism (beam splitter cube)", "Reflects the LCD towards your eye while you see through it. Uncollimated: the image sits at the cube, not at infinity.",
      B(25, 25, 25), 0x9fd9ff, [0, 26, 0], [0, 40, 0], { transparent: true, opacity: 0.28, roughness: 0.05, metalness: 0.2 });
    part(desk, "QMI8658 IMU", "6-axis accel+gyro on I2C GPIO47/48. 200 Hz Mahony fusion gives pitch/roll; yaw needs a heading reference.",
      B(3, 1, 2.5), 0x222222, [12, 12.5, 12], [30, 10, 30]);
    part(desk, "Backpack PCB (V2)", "Plugs onto the GPIO headers: GNSS, magnetometer, fuel gauge, encoder, haptic. hardware/backpack/.",
      B(40, 1.6, 40), 0x0f5a2a, [0, -2, 0], [0, -22, 0]);
    part(desk, "u-blox SAM-M10Q GNSS", "Own position + time. Integrated patch antenna: point it at the sky.",
      B(15.5, 6.3, 15.5), 0xc9a44a, [-8, -6, -8], [0, -34, 0], { metalness: 0.6 });
    part(desk, "MMC5983MA magnetometer", "Absolute heading. Placed at the far corner from the battery and motor.",
      B(3, 1, 3), 0x333333, [15, -3.3, 15], [18, -30, 18]);
    part(desk, "MAX17048 fuel gauge", "Battery state of charge over I2C.", B(2, 1, 2), 0x333333, [-15, -3.3, 14], [-10, -30, 10]);
    part(desk, "1S LiPo 1200-2000 mAh", "Plugs into the Waveshare MX1.25 BAT connector; ~7 h HUD-only.",
      B(50, 8, 34), 0xb8bcc2, [0, -10, 0], [0, -55, 0], { metalness: 0.5, roughness: 0.3 });
    hw.add(desk);

    // Rail build: Picatinny rail + housing + Boson + core board
    const rail = new THREE.Group();
    const railBar = part(rail, "Picatinny rail (MIL-STD-1913)", "21.2 mm wide, slots 5.23 mm on a 10.01 mm pitch. M-LOK handguards take a short M-LOK-to-Picatinny section.",
      B(260, 8, 21.2), 0x4a4a4a, [0, -4, 0], [0, -10, 0], { metalness: 0.5 });
    for (let i = -12; i <= 12; i++) {
      const s = new THREE.Mesh(B(5.23, 3, 22), new THREE.MeshStandardMaterial({ color: 0x111111 }));
      s.position.set(i * 10.01 * mm, 1 * mm, 0);
      railBar.add(s);
    }
    part(rail, "QD Picatinny clamp", "Lever clamp with recoil lug: repeatable return-to-zero, so HUD and thermal boresight survive remounting.",
      B(40, 12, 34), 0x3a3f36, [0, 6, 0], [0, 10, 0]);
    part(rail, "Housing (concept)", "Rigid chassis ties Boson, IMU and eyepiece together. hardware/mount/hud_rail_mount.scad.",
      B(150, 46, 44), 0x4b5a4e, [0, 35, 0], [0, 60, 0], { transparent: true, opacity: 0.18 });
    part(rail, "FLIR Boson 640 (21640AS50)", "640×512 LWIR, 50° HFOV, 60 Hz, 1.8 V CMOS or USB via VPC. 500-1550 mW.",
      B(21, 21, 21), 0x6d6d6d, [55, 40, 0], [60, 60, 0], { metalness: 0.4 });
    const lens = part(rail, "Boson lens (50° HFOV)", "Short lens; the HUD crops the centre ~499 px to match a 40° display.",
      new THREE.CylinderGeometry(8 * mm, 9 * mm, 18 * mm, 32), 0x1c1c1c, [74, 40, 0], [85, 60, 0], { metalness: 0.3 });
    lens.rotation.z = Math.PI / 2;
    part(rail, "Boson USB VPC (421-0061-00)", "USB-C video/power/control. Bench + configuration; direct UVC input on an ESP32-P4.",
      B(21, 4, 21), 0x0f5a2a, [36, 40, 0], [40, 60, 0]);
    part(rail, "HUD core board (V3)", "ESP32-P4 + C6 recommended with thermal (USB-HS UVC, PPA blending); ESP32-S3 without.",
      B(45, 2, 32), 0x0f5a2a, [-10, 28, 0], [0, 70, 0]);
    part(rail, "IMU + magnetometer (rigid)", "IMU on the rigid chassis; magnetometer high and rear, away from barrel steel.",
      B(6, 2, 6), 0x222222, [-45, 50, 0], [-20, 90, 0]);
    part(rail, "LiPo 2000 mAh", "Low centre of gravity; ~4-5 h with the Boson.", B(70, 10, 36), 0xb8bcc2, [-5, 18, 0], [0, 30, 0], { metalness: 0.5 });
    const eyep = part(rail, "Micro-display + eyepiece", "Collimated view at rail eye relief (the Waveshare prism is a desk demo).",
      new THREE.CylinderGeometry(13 * mm, 13 * mm, 30 * mm, 32), 0x222222, [-90, 40, 0], [-110, 60, 0]);
    eyep.rotation.z = Math.PI / 2;
    hw.add(rail);


    // Hub build (V2H): Waveshare head on the hub backpack, 3 USB-C ports,
    // ESP32-P4, 2x18650, Boson+VPC and the ATAK phone on cables.
    const hub = new THREE.Group();
    part(hub, "Waveshare HUD head (ESP32-S3)", "Display, IMU, Wi-Fi/TAK and renderer. Powered with 5 V from the hub via its USB VCC header pin.",
      B(40, 12, 40), 0x2a2f36, [0, 30, 0], [0, 40, 0]);
    part(hub, "Prism", "Beam splitter over the LCD.", B(25, 25, 25), 0x9fd9ff, [0, 48.5, 0], [0, 70, 0],
      { transparent: true, opacity: 0.28, roughness: 0.05 });
    part(hub, "Hub backpack PCB (V2H)", "Charger + power path, 5 V boost, 3.3 V buck-boost, 3 USB-C ports, header link to the HUD. hardware/hub_backpack/.",
      B(70, 1.6, 50), 0x0f5a2a, [0, 20, 0], [0, 10, 0]);
    part(hub, "ESP32-P4 hub processor", "USB-HS host for the Boson (UVC), FS device to the phone (CDC), crops/scales thermal to 240x240 and streams it to the HUD over SPI.",
      B(18, 3, 18), 0x333a40, [-18, 22.3, 8], [-10, 20, 10], { metalness: 0.3 });
    part(hub, "SAM-M10Q GNSS", "Own position when the phone is unplugged; UTC for TLS.", B(15.5, 6.3, 15.5), 0xc9a44a, [20, 24, 12], [20, 25, 10], { metalness: 0.6 });
    part(hub, "USB-C CAM (host, 5 V out)", "Boson VPC plugs in here. 1.5 A current-limited VBUS (TPS25200).", B(9, 3.2, 7.5), 0xb0b0b0, [35, 18, -12], [25, 0, 0], { metalness: 0.8 });
    part(hub, "USB-C PHONE (device)", "ATAK phone: CDC serial for tracks/own position; optional UVC thermal preview.", B(9, 3.2, 7.5), 0xb0b0b0, [35, 18, 0], [25, 0, 0], { metalness: 0.8 });
    part(hub, "USB-C PWR IN (charge)", "5 V/3 A charging; system keeps running (power path).", B(9, 3.2, 7.5), 0xb0b0b0, [35, 18, 12], [25, 0, 0], { metalness: 0.8 });
    [-10, 10].forEach((z, i) => {
      const c = part(hub, "18650 Li-ion cell " + (i + 1), "1S2P pack, 6-7 Ah (~25 Wh): ~7-8 h with thermal on.",
        new THREE.CylinderGeometry(9 * mm, 9 * mm, 65 * mm, 24), 0x3565a8, [0, 9, z], [0, -25, z * 1.5], { metalness: 0.4 });
      c.rotation.z = Math.PI / 2;
    });
    part(hub, "FLIR Boson 640 + lens", "Powered and read over USB by the hub (VPC now; direct 80-pin connector later).",
      B(21, 21, 21), 0x6d6d6d, [95, 20, -40], [30, 0, -20], { metalness: 0.4 });
    part(hub, "Boson USB VPC (421-0061-00)", "USB-C video/power/control board behind the camera.", B(21, 4, 21), 0x0f5a2a, [95, 7, -40], [30, -8, -20]);
    part(hub, "ATAK phone", "USB-C to the PHONE port. The plugin sends tracks and its GPS; the HUD sends back attitude.",
      B(75, 8, 160), 0x111418, [-40, 4, 110], [-30, 0, 40], { roughness: 0.3 });
    function cable(a, b) {
      const pts = [a, new THREE.Vector3((a.x + b.x) / 2, Math.min(a.y, b.y) - 10, (a.z + b.z) / 2), b].map((v) => v.clone().multiplyScalar(mm));
      hub.add(new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 24, 1.6 * mm, 8), new THREE.MeshStandardMaterial({ color: 0x222222 })));
    }
    cable(new THREE.Vector3(40, 18, -12), new THREE.Vector3(95, 7, -30));
    cable(new THREE.Vector3(40, 18, 0), new THREE.Vector3(-10, 6, 40));
    hw.add(hub);

    let explode = 0;
    function setBuild(which) {
      desk.visible = which === "desk";
      rail.visible = which === "rail";
      hub.visible = which === "hub";
      const cam = { desk: [1.2, 0.9, 1.3], rail: [1.9, 1.1, 2.0], hub: [2.2, 1.6, 2.3] }[which];
      hwCam.position.set(cam[0], cam[1], cam[2]);
      hwCtl.target.set(which === "hub" ? 0.2 : 0, which === "desk" ? 0.05 : which === "hub" ? 0.1 : 0.3, which === "hub" ? 0.2 : 0);
      hwCtl.update();
    }
    document.querySelectorAll("[data-build]").forEach((b) =>
      b.addEventListener("click", () => {
        setBuild(b.dataset.build);
        document.querySelectorAll("[data-build]").forEach((x) => x.classList.toggle("on", x === b));
      })
    );
    $("explode").addEventListener("input", (e) => (explode = parseFloat(e.target.value)));
    setBuild("hub");

    const ray = new THREE.Raycaster(), mouse = new THREE.Vector2();
    let picked = null;
    hwR.domElement.addEventListener("click", (ev) => {
      const r = hwR.domElement.getBoundingClientRect();
      mouse.set(((ev.clientX - r.left) / r.width) * 2 - 1, -((ev.clientY - r.top) / r.height) * 2 + 1);
      ray.setFromCamera(mouse, hwCam);
      const hit = ray.intersectObjects(parts.filter((p) => p.parent.visible), false)[0];
      if (picked) picked.material.emissive && picked.material.emissive.setHex(0x000000);
      picked = hit ? hit.object : null;
      if (picked) {
        picked.material.emissive.setHex(0x224422);
        $("hwInfo").innerHTML = `<b>${picked.userData.name}</b><br>${picked.userData.desc}`;
      }
    });

    // ============================================================ live attitude view
    const attHost = $("liveAttView");
    const att = new THREE.Scene();
    att.background = new THREE.Color(0x0d1210);
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
    const fwd = new THREE.ArrowHelper(new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, 0.2, 0), 0.7, 0x00ff5a, 0.1, 0.06);
    dev.add(fwd);
    att.add(dev);
    att.add(new THREE.AxesHelper(0.9));
    const nLab = new THREE.ArrowHelper(new THREE.Vector3(0, 0, -1), new THREE.Vector3(0, -0.3, 0), 0.9, 0x5f7fff, 0.08, 0.05);
    att.add(nLab);
    att.add(new THREE.GridHelper(2, 8, 0x2e4a36, 0x223528));

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
        hwCtl.update();
        hwR.render(hw, hwCam);
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
