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

    // ---------------------------------------------------------- rail build: side pod + combiner ahead of an EOTech
    // +X is towards the muzzle, +Z is the pod side. The holographic sight stays
    // at the rear. Camera, display and electronics live in a side pod; only a
    // thin beam-splitter combiner sits in the sight line, so the day view stays
    // clear and the thermal/TAK image is overlaid on it.
    const rail = new THREE.Group();
    const RAIL_TOP = 8, AXIS_Y = RAIL_TOP + 36; // sight axis ≈36 mm over the rail (verify on your XPS2)
    const railBar = part(rail, "Picatinny rail (MIL-STD-1913)", "21.2 mm wide, slots 5.23 mm on a 10.01 mm pitch.",
      B(300, 8, 21.2), 0x3c3f43, [0, 4, 0], [0, -10, 0], { metalness: 0.6, roughness: 0.4 }, { label: "Picatinny rail" });
    for (let i = -14; i <= 14; i++) {
      const s = new THREE.Mesh(B(5.23, 3, 22), new THREE.MeshStandardMaterial({ color: 0x15171a }));
      s.position.set(i * 10.01 * mm, 3 * mm, 0);
      railBar.add(s);
    }

    // EOTech XPS2-style holographic sight at the rear (≈89 × 53 × 64 mm)
    const EX = -95;
    part(rail, "Holographic sight (EOTech HWS XPS2), user side", "Unchanged aiming optic closest to the eye. 1× and parallax-free, so the overlay must be collimated.",
      B(89, 22, 44), 0x2e3136, [EX, RAIL_TOP + 11, 0], [0, 0, 0], { roughness: 0.5 }, { label: "EOTech XPS2 (user side)" });
    const hoodMat = { roughness: 0.45, metalness: 0.2 };
    part(rail, "Sight hood (left)", "Hood around the holographic window.", B(40, 44, 5), 0x2e3136, [EX + 20, RAIL_TOP + 44, 21], [0, 0, 0], hoodMat);
    part(rail, "Sight hood (right)", "Hood around the holographic window.", B(40, 44, 5), 0x2e3136, [EX + 20, RAIL_TOP + 44, -21], [0, 0, 0], hoodMat);
    part(rail, "Sight hood (top)", "Hood around the holographic window.", B(40, 5, 47), 0x2e3136, [EX + 20, RAIL_TOP + 66, 0], [0, 0, 0], hoodMat);
    part(rail, "Holographic window", "≈30 × 23 mm clear aperture.", B(3, 38, 37), 0x9fd7ff, [EX + 20, AXIS_Y, 0], [0, 0, 0], MAT.glass);
    part(rail, "Sight battery housing", "Transverse battery cap.", new THREE.CylinderGeometry(11 * mm, 11 * mm, 53 * mm, 32), 0x2e3136, [EX - 22, RAIL_TOP + 11, 0], [0, 0, 0])
      .rotation.x = Math.PI / 2;

    // Combiner in the sight line: the only thing in front of the EOTech
    const CBX = EX + 60;
    const comb = part(rail, "Beam-splitter combiner (flip-up)", "≈40 × 40 mm plate beam splitter at 45°, ~70 % transmission: the day view passes straight through, the collimated display image from the side pod reflects into the sight line. Flips up out of the way when not needed.",
      B(1.5, 40, 44), 0x9fd7ff, [CBX, AXIS_Y, 0], [0, 0, 0], { transparent: true, opacity: 0.35, roughness: 0.02, metalness: 0.3, depthWrite: false }, { label: "Combiner (flip-up)" });
    comb.rotation.y = Math.PI / 4;
    part(rail, "Combiner arm", "Rigid arm from the pod; hinge lets the combiner flip up.", B(12, 4, 34), 0x55595e, [CBX, AXIS_Y + 24, 14], [0, 0, 0], { metalness: 0.5 });

    // Side pod: camera, collimated display, electronics, battery
    const PZ = 58, PX = CBX + 35;
    part(rail, "Offset mount (top rail → side pod)", "Lever-lock Picatinny base with an offset arm to the side pod. Repeatable return keeps the thermal/display boresight.",
      B(40, 12, 34), 0x55595e, [PX - 10, RAIL_TOP + 6, 0], [0, 10, 0], { metalness: 0.5 }, { label: "Offset QD mount" });
    part(rail, "Offset arm", "Carries the pod beside the sight line.", B(24, 10, 44), 0x55595e, [PX - 10, RAIL_TOP + 12, 30], [0, 10, 10], { metalness: 0.5 });
    part(rail, "Side pod housing", "Rigid chassis for the Boson, display, collimating lens and IMU. Nothing but the combiner obstructs the sight picture.",
      B(120, 48, 62), 0x9aa3ab, [PX + 5, AXIS_Y, PZ], [0, 20, 40], { transparent: true, opacity: 0.22, depthWrite: false }, { label: "Side pod" });
    part(rail, "FLIR Boson 640 (21640AS50)", `Faces downrange from the side, ≈${PZ} mm off the sight axis. Parallax is fixed in software: 0.03° at 100 m, 0.13° at 25 m, 0.6° at 5 m.`,
      B(21, 21, 21), 0x7d8288, [PX + 45, AXIS_Y, PZ], [30, 20, 40], { metalness: 0.5 }, { label: "Boson 640 (side)" });
    const lens = part(rail, "Boson lens", "50° HFOV lens.", new THREE.CylinderGeometry(8 * mm, 9 * mm, 18 * mm, 32), 0x202225,
      [PX + 64, AXIS_Y, PZ], [50, 20, 40], { metalness: 0.4 });
    lens.rotation.z = Math.PI / 2;
    const obj = part(rail, "Collimating objective lens", "f ≈ 50 mm, ≥ 32 mm clear aperture, facing the combiner: puts the display image at infinity.",
      new THREE.CylinderGeometry(18 * mm, 18 * mm, 6 * mm, 48), 0x8fd3ff, [CBX, AXIS_Y, PZ - 30], [0, 20, 30], MAT.glass, { label: "Collimating lens" });
    obj.rotation.x = Math.PI / 2;
    part(rail, "Micro-OLED display", "At the lens focal point, facing the combiner. Shows hot-only thermal + TAK symbology: black pixels add no light, so the day view stays clear.",
      B(18, 14, 3), 0x111214, [CBX, AXIS_Y, PZ + 20], [0, 20, 40], { roughness: 0.2 }, { label: "Micro-OLED" });
    part(rail, "HUD core board (ESP32-P4 + C6)", "UVC or DVP from the Boson, alignment warp + blend in the PPA, MIPI-DSI to the micro-OLED, Wi-Fi/TAK.",
      B(45, 2, 32), 0x1d6b3e, [PX + 10, AXIS_Y - 18, PZ], [0, 20, 40], MAT.pcb);
    part(rail, "IMU + magnetometer", "Rigid on the pod chassis.", B(6, 2, 6), 0x222222, [PX + 30, AXIS_Y + 20, PZ], [0, 30, 40]);
    part(rail, "LiPo 2000-3000 mAh", "Inside the pod.", B(50, 10, 34), 0xc9ccd1, [PX + 12, AXIS_Y + 12, PZ], [0, 30, 40], { metalness: 0.6 });

    // Light path: display → lens → combiner, then combiner → sight → eye
    const beamMat = new THREE.MeshBasicMaterial({ color: 0x38bdf8, transparent: true, opacity: 0.2, side: THREE.DoubleSide, depthWrite: false });
    const b1 = new THREE.Mesh(new THREE.CylinderGeometry(14 * mm, 14 * mm, (PZ - 30) * mm, 32, 1, true), beamMat);
    b1.rotation.x = Math.PI / 2;
    b1.position.set(CBX * mm, AXIS_Y * mm, ((PZ - 30) / 2) * mm);
    const eyeX = EX - 44.5 - 75, lenB = CBX - eyeX;
    const b2 = new THREE.Mesh(new THREE.CylinderGeometry(14 * mm, 14 * mm, lenB * mm, 32, 1, true), beamMat);
    b2.rotation.z = Math.PI / 2;
    b2.position.set((CBX - lenB / 2) * mm, AXIS_Y * mm, 0);
    b1.userData.noFit = b2.userData.noFit = true;
    rail.add(b1, b2);
    const eyeLab = label("◄ to eye (~75 mm)", "");
    eyeLab.position.set((EX - 44 - 25) * mm, (AXIS_Y + 22) * mm, 0);
    rail.add(eyeLab);
    hw.add(rail);

    // ---------------------------------------------------------- ATAK phone: Galaxy S20+ in a tan Juggernaut case
    // S20+ is 161.9 × 73.7 × 7.8 mm; the rugged case adds bumpers and a raised lip
    // (≈172 × 86 × 15 mm overall). Lying face up, long axis along Z.
    function atakScreenTexture() {
      const c = document.createElement("canvas");
      c.width = 360; c.height = 780;
      const x = c.getContext("2d");
      x.fillStyle = "#1d2621"; x.fillRect(0, 0, 360, 780);
      // imagery-ish map: terrain patches, roads, grid
      const patches = [["#26352b", 30, 120, 180, 140], ["#2c3a30", 170, 300, 170, 160], ["#233027", 20, 470, 220, 150], ["#2f3d2f", 200, 560, 150, 170]];
      patches.forEach(([col, px, py, w, h]) => { x.fillStyle = col; x.fillRect(px, py, w, h); });
      x.strokeStyle = "#6b6450"; x.lineWidth = 6;
      x.beginPath(); x.moveTo(0, 520); x.bezierCurveTo(120, 470, 200, 420, 360, 300); x.stroke();
      x.lineWidth = 3; x.beginPath(); x.moveTo(140, 780); x.lineTo(180, 60); x.stroke();
      x.strokeStyle = "rgba(255,255,255,.08)"; x.lineWidth = 1;
      for (let i = 0; i < 360; i += 45) { x.beginPath(); x.moveTo(i, 0); x.lineTo(i, 780); x.stroke(); }
      for (let j = 0; j < 780; j += 45) { x.beginPath(); x.moveTo(0, j); x.lineTo(360, j); x.stroke(); }
      // units: friendly rectangles, hostile diamond, self arrow
      const fr = (px, py) => { x.fillStyle = "#38bdf8"; x.strokeStyle = "#0b2a3a"; x.lineWidth = 2; x.fillRect(px - 13, py - 9, 26, 18); x.strokeRect(px - 13, py - 9, 26, 18); };
      fr(150, 330); fr(95, 260); fr(250, 210);
      x.fillStyle = "#ff6b6b"; x.beginPath(); x.moveTo(190, 120); x.lineTo(206, 136); x.lineTo(190, 152); x.lineTo(174, 136); x.closePath(); x.fill();
      x.fillStyle = "#ffffff"; x.beginPath(); x.moveTo(180, 470); x.lineTo(196, 510); x.lineTo(180, 500); x.lineTo(164, 510); x.closePath(); x.fill();
      x.strokeStyle = "rgba(255,255,255,.6)"; x.setLineDash([6, 6]); x.beginPath(); x.moveTo(180, 470); x.lineTo(190, 150); x.stroke(); x.setLineDash([]);
      // ATAK-style chrome
      x.fillStyle = "rgba(0,0,0,.72)"; x.fillRect(0, 0, 360, 58); x.fillRect(0, 722, 360, 58);
      x.fillStyle = "#e8e8e8"; x.font = "bold 22px Bahnschrift, Segoe UI, sans-serif"; x.fillText("ATAK", 16, 38);
      x.font = "15px Bahnschrift, Segoe UI, sans-serif"; x.fillStyle = "#bdbdbd"; x.fillText("KYLE · 10S EG 9831 4512", 92, 37);
      x.fillStyle = "#34d399"; x.beginPath(); x.arc(334, 30, 7, 0, Math.PI * 2); x.fill();
      ["⊕", "◎", "✎", "☰"].forEach((g, i) => { x.fillStyle = "#e0e0e0"; x.font = "26px Segoe UI Symbol, sans-serif"; x.fillText(g, 34 + i * 88, 762); });
      const t = new THREE.CanvasTexture(c);
      t.colorSpace = THREE.SRGBColorSpace;
      t.anisotropy = 4;
      return t;
    }
    const RBox = addons.RoundedBoxGeometry;
    const RB = (w, h, d, r) => (RBox ? new RBox(w * mm, h * mm, d * mm, 4, r * mm) : B(w, h, d));
    function makePhone(group, pos, explode) {
      const TAN = 0xb99b6b;
      const body = part(group, "ATAK phone: Samsung Galaxy S20+ in a tan Juggernaut case",
        "USB-C to the hub's PHONE port. The ATAK plugin sends tracks and the phone's GPS; the HUD sends back its attitude. Rugged case: raised bumpers and lip around the screen, strap plate on the back.",
        RB(86, 15, 172, 7), TAN, pos, explode, { roughness: 0.85, metalness: 0.0 }, { label: "ATAK phone (S20+, tan Juggernaut)" });
      const top = 7.5;
      // corner bumpers
      [[-1, -1], [1, -1], [-1, 1], [1, 1]].forEach(([sx, sz]) => {
        const bump = new THREE.Mesh(RB(20, 17, 24, 5), new THREE.MeshStandardMaterial({ color: 0xa88a5c, roughness: 0.9 }));
        bump.position.set(sx * 35 * mm, 0, sz * 76 * mm);
        bump.castShadow = true;
        body.add(bump);
      });
      // glass + screen (inset under the case lip)
      const glass = new THREE.Mesh(RB(72, 1.2, 158, 5), new THREE.MeshStandardMaterial({ color: 0x050607, roughness: 0.08, metalness: 0.2 }));
      glass.position.y = (top - 0.3) * mm;
      body.add(glass);
      const scr = new THREE.Mesh(new THREE.PlaneGeometry(66 * mm, 146 * mm),
        new THREE.MeshBasicMaterial({ map: atakScreenTexture(), toneMapped: false }));
      scr.rotation.x = -Math.PI / 2;
      scr.position.y = (top + 0.35) * mm;
      body.add(scr);
      // punch-hole camera and side buttons
      const hole = new THREE.Mesh(new THREE.CylinderGeometry(1.6 * mm, 1.6 * mm, 0.4 * mm, 16), new THREE.MeshBasicMaterial({ color: 0x000000 }));
      hole.position.set(0, (top + 0.4) * mm, -75.5 * mm);
      body.add(hole);
      const btn = new THREE.Mesh(B(3, 5, 22), new THREE.MeshStandardMaterial({ color: 0x8f7650, roughness: 0.9 }));
      btn.position.set(44 * mm, 1 * mm, -30 * mm);
      body.add(btn);
      // USB-C port cut-out at the bottom edge
      const port = new THREE.Mesh(B(9, 3.4, 2), new THREE.MeshBasicMaterial({ color: 0x111111 }));
      port.position.set(0, 0, 86.2 * mm);
      body.add(port);
      return body;
    }

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
    makePhone(hub, [-125, 7.5, 5], [-30, 0, 0]);
    function cable(a, b) {
      const pts = [a, new THREE.Vector3((a.x + b.x) / 2, Math.min(a.y, b.y) - 6, (a.z + b.z) / 2), b].map((v) => v.clone().multiplyScalar(mm));
      const m = new THREE.Mesh(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 32, 1.6 * mm, 10), new THREE.MeshStandardMaterial({ color: 0x2b2d31, roughness: 0.6 }));
      m.castShadow = true;
      hub.add(m);
    }
    cable(new THREE.Vector3(toX(52), top + 1.6, edgeZ + 2), new THREE.Vector3(toX(52) - 20, 23, edgeZ + 34));
    cable(new THREE.Vector3(toX(64), top + 1.6, edgeZ + 2), new THREE.Vector3(-125, 7.5, 93));
    hw.add(hub);

    // ---------------------------------------------------------- build switch, fit, pick
    let explode = 0, current = "hub";
    const groups = { desk, rail, hub };
    function fitView(which) {
      const box = new THREE.Box3();
      groups[which].traverse((o) => { if (o.isMesh && !o.userData.noFit && !(o.parent && o.parent.userData.noFit)) box.expandByObject(o); });
      const c = box.getCenter(new THREE.Vector3()), size = box.getSize(new THREE.Vector3());
      const r = Math.max(size.x, size.y * 1.4, size.z) * 0.5;
      const dist = (r / Math.tan((hwCam.fov * Math.PI) / 360)) * (which === "rail" ? 0.72 : 1.15);
      const dir = (which === "rail" ? new THREE.Vector3(0.25, 0.6, 1) : new THREE.Vector3(1, 0.85, 1.15)).normalize();
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
