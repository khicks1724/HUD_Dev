/*
 * pcb.js - placement study for the V2H hub backpack PCB
 * (hardware/hub_backpack/README.md). One data set drives the 2D layout view
 * and the 3D hub model, so they always agree.
 *
 * Units: mm. Origin top-left of the board, x right, y down (as in KiCad).
 * This is a floor plan for layout planning, not a routed board.
 */
(function () {
  "use strict";
  const PCB = (window.PCB = {});

  PCB.board = { w: 84, h: 54, thickness: 1.6, layers: 4, name: "V2H hub backpack rev A (placement study)" };
  // Where the Waveshare HUD head sits above the board on its headers.
  PCB.head = { x: 2, y: 4, w: 42, h: 46, stack: 8.5 };

  // kind: ic | module | conn | passive | mech | usb
  PCB.parts = [
    { ref: "U1", name: "ESP32-P4 core module", part: "ESP32-P4 (32 MB PSRAM) castellated module", x: 24, y: 26, w: 18, h: 18, z: 3.2, kind: "module", grp: "compute",
      why: "Hub processor. USB-HS host for the Boson, USB-FS device for the phone, PPA crop/scale, SPI master to the HUD. Under the HUD head: it's flat and has no RF, so the space is free." },
    { ref: "U9", name: "GNSS", part: "u-blox SAM-M10Q", x: 70, y: 13, w: 15.5, h: 15.5, z: 6.3, kind: "module", grp: "sensor",
      why: "Integrated patch antenna. Top-right corner, outside the head footprint, so it has open sky and sits far from the boost inductor." },
    { ref: "U10", name: "Magnetometer", part: "MEMSIC MMC5983MA", x: 50, y: 8, w: 3, h: 3, z: 1, kind: "ic", grp: "sensor",
      why: "Heading. >20 mm from L1/L3 and the cells; copper keep-out under it; axes aligned with the HUD IMU. Wired to the HUD's I2C (GPIO1/2)." },
    { ref: "U3", name: "Charger + power path", part: "TI BQ25895", x: 72, y: 34, w: 4, h: 4, z: 0.9, kind: "ic", grp: "power",
      why: "1S charger with power path, next to the PWR IN connector so the high-current input loop stays short." },
    { ref: "L3", name: "Charger inductor", part: "2.2 µH 4×4 mm", x: 79, y: 34, w: 4, h: 4, z: 2, kind: "passive", grp: "power", why: "Charger buck inductor." },
    { ref: "U4", name: "5 V boost", part: "TI TPS61088", x: 60, y: 30, w: 4.5, h: 3.5, z: 0.9, kind: "ic", grp: "power",
      why: "5 V / 3 A for the camera port and the HUD head. Tight loop with L1 and the output caps." },
    { ref: "L1", name: "Boost inductor", part: "1 µH 7×7 mm shielded", x: 52, y: 31, w: 7, h: 7, z: 3, kind: "passive", grp: "power",
      why: "Largest magnetic field on the board: kept away from U10 and the GNSS." },
    { ref: "U5", name: "3.3 V buck-boost", part: "TI TPS63802", x: 68, y: 25, w: 3, h: 2, z: 0.9, kind: "ic", grp: "power", why: "3.3 V for the P4 and GNSS, regulating down to an empty cell." },
    { ref: "L2", name: "3.3 V inductor", part: "0.47 µH 2.5×2 mm", x: 73, y: 25, w: 2.5, h: 2, z: 1.2, kind: "passive", grp: "power", why: "TPS63802 inductor." },
    { ref: "U6", name: "CAM VBUS switch", part: "TI TPS25200", x: 54, y: 39, w: 2, h: 2, z: 0.8, kind: "ic", grp: "power",
      why: "1.5 A current limit and fault flag for the camera port; P4 can switch the camera off." },
    { ref: "U7", name: "Ideal diode to HUD 5 V", part: "TI LM66100", x: 40, y: 46, w: 2, h: 2, z: 0.8, kind: "ic", grp: "power",
      why: "Feeds 5 V to the Waveshare USB VCC header pin and blocks back-feed when its own USB-C is plugged in." },
    { ref: "U8", name: "Fuel gauge", part: "MAX17048", x: 26, y: 48, w: 2, h: 2, z: 0.8, kind: "ic", grp: "power", why: "Battery state of charge, next to the battery connector." },
    { ref: "U11", name: "ESD (CAM)", part: "TPD2EUSB30", x: 52, y: 43, w: 2, h: 2, z: 0.6, kind: "ic", grp: "usb", why: "ESD clamp right at the connector." },
    { ref: "U12", name: "ESD (PHONE)", part: "TPD2EUSB30", x: 64, y: 43, w: 2, h: 2, z: 0.6, kind: "ic", grp: "usb", why: "ESD clamp right at the connector." },
    { ref: "U13", name: "ESD (PWR IN)", part: "USBLC6-2", x: 76, y: 43, w: 2.9, h: 1.6, z: 0.6, kind: "ic", grp: "usb", why: "ESD clamp right at the connector." },
    { ref: "J1", name: "USB-C CAM (host)", part: "USB-C 16-pin mid-mount", x: 52, y: 50.5, w: 9, h: 7.5, z: 3.2, kind: "usb", grp: "usb",
      why: "Boson VPC plugs in here. USB2 High-Speed pair routed 90 Ω differential to U1, as short as possible." },
    { ref: "J2", name: "USB-C PHONE (device)", part: "USB-C 16-pin mid-mount", x: 64, y: 50.5, w: 9, h: 7.5, z: 3.2, kind: "usb", grp: "usb",
      why: "ATAK phone. Full-Speed pair to U1. Rd on CC, VBUS sense only." },
    { ref: "J3", name: "USB-C PWR IN", part: "USB-C 16-pin mid-mount", x: 76, y: 50.5, w: 9, h: 7.5, z: 3.2, kind: "usb", grp: "usb", why: "5 V / 3 A charge input to U3." },
    { ref: "J4", name: "Battery", part: "JST-PH 2-pin", x: 12, y: 50, w: 6, h: 4.5, z: 6, kind: "conn", grp: "power", why: "1S2P 18650 pack mounted under the board." },
    { ref: "H1", name: "HUD header H1", part: "1×8 2.54 mm female", x: 15, y: 8, w: 20.3, h: 2.54, z: 8.5, kind: "conn", grp: "link",
      why: "Mates the Waveshare GPIO header. Verify pin order against the Waveshare schematic before layout." },
    { ref: "H2", name: "HUD header H2", part: "1×8 2.54 mm female", x: 15, y: 44, w: 20.3, h: 2.54, z: 8.5, kind: "conn", grp: "link",
      why: "UART1 12/13, SPI3 9/10/11, READY 14 and 5 V (USB VCC) to the HUD head." },
    { ref: "SW1", name: "User button", part: "6×6 mm tactile", x: 46, y: 21, w: 6, h: 6, z: 4.3, kind: "mech", grp: "link", why: "Mode / align button to HUD GPIO3." },
    { ref: "M1", name: "Haptic motor pads", part: "10 mm ERM, AO3400 driver", x: 46, y: 13, w: 4, h: 2, z: 0.3, kind: "passive", grp: "link", why: "Vibration alerts on HUD GPIO4." },
  ];

  PCB.holes = [[3.5, 3.5], [80.5, 3.5], [3.5, 50.5], [80.5, 50.5]];

  // Net classes and representative routes (polylines, mm).
  PCB.nets = {
    hs: { label: "USB2 HS pair (90 Ω diff)", color: "#1f8fe0", width: 0.35, pair: true },
    fs: { label: "USB FS pair", color: "#56b4f0", width: 0.3, pair: true },
    spi: { label: "SPI + UART + READY to HUD", color: "#8a5cf6", width: 0.25 },
    i2c: { label: "I2C (magnetometer → HUD)", color: "#0ea37a", width: 0.25 },
    p5v: { label: "5 V (boost → camera, HUD)", color: "#e0561f", width: 1.2 },
    vbat: { label: "VBAT / VSYS", color: "#c9302c", width: 1.2 },
  };
  PCB.routes = [
    { net: "hs", pts: [[52, 46.5], [52, 41], [44, 34], [33, 30]] },
    { net: "fs", pts: [[64, 46.5], [64, 40], [58, 36], [46, 27], [33, 24]] },
    { net: "spi", pts: [[20, 35], [20, 40], [14, 42.5]] },
    { net: "spi", pts: [[22, 35], [22, 41], [16.5, 42.5]] },
    { net: "spi", pts: [[24, 35], [24, 42], [19, 42.5]] },
    { net: "spi", pts: [[26, 35], [26, 42.5], [21.5, 42.5]] },
    { net: "i2c", pts: [[48.5, 8], [30, 8], [26, 9]] },
    { net: "p5v", pts: [[62.3, 30], [62.3, 36], [55, 38], [52, 46.5]] },
    { net: "p5v", pts: [[55, 38], [41, 44], [40, 45]] },
    { net: "p5v", pts: [[39, 46], [24, 46], [24, 44.8]] },
    { net: "vbat", pts: [[12, 47.5], [12, 38], [48, 36], [49, 33]] },
    { net: "vbat", pts: [[70, 34], [56, 33]] },
    { net: "vbat", pts: [[76, 46.5], [74, 36]] },
  ];

  PCB.keepouts = [
    { kind: "circle", x: 50, y: 8, r: 6, label: "MAG KEEP-OUT (no pour, no steel)" },
    { kind: "rect", x: 62, y: 5, w: 16, h: 16, label: "GNSS: SKY VIEW" },
  ];

  PCB.groups = {
    compute: { label: "Compute", color: "#2b2f33" },
    sensor: { label: "Sensors", color: "#3f6fb0" },
    power: { label: "Power", color: "#8a3b2e" },
    usb: { label: "USB-C / ESD", color: "#6b6b6b" },
    link: { label: "HUD link / UI", color: "#5b4a8a" },
  };

  // ----------------------------------------------------------- 2D SVG render
  const S = 10; // px per mm in the SVG user space
  const esc = (t) => String(t).replace(/&/g, "&amp;").replace(/</g, "&lt;");

  function footprint(p) {
    const x = (p.x - p.w / 2) * S, y = (p.y - p.h / 2) * S, w = p.w * S, h = p.h * S;
    let pads = "";
    if (p.kind === "ic" || p.kind === "module") {
      const n = p.kind === "module" ? 9 : Math.max(2, Math.round(p.w * 1.5));
      const step = w / (n + 1), pad = Math.min(step * 0.55, 9);
      for (let i = 1; i <= n; i++) {
        const px = x + i * step - pad / 2;
        pads += `<rect class="pad" x="${px}" y="${y - 3}" width="${pad}" height="6"/><rect class="pad" x="${px}" y="${y + h - 3}" width="${pad}" height="6"/>`;
        if (p.kind === "module") {
          const py = y + i * (h / (n + 1)) - pad / 2;
          pads += `<rect class="pad" x="${x - 3}" y="${py}" width="6" height="${pad}"/><rect class="pad" x="${x + w - 3}" y="${py}" width="6" height="${pad}"/>`;
        }
      }
    } else if (p.kind === "conn" && p.ref.startsWith("H")) {
      for (let i = 0; i < 8; i++) pads += `<circle class="pad" cx="${x + (i + 0.5) * 25.4}" cy="${y + h / 2}" r="8"/><circle class="drill" cx="${x + (i + 0.5) * 25.4}" cy="${y + h / 2}" r="4.5"/>`;
    } else if (p.kind === "usb") {
      for (let i = 0; i < 12; i++) pads += `<rect class="pad" x="${x + 10 + i * 6}" y="${y - 6}" width="3" height="10"/>`;
      pads += `<rect class="pad" x="${x - 4}" y="${y + 18}" width="8" height="22" rx="3"/><rect class="pad" x="${x + w - 4}" y="${y + 18}" width="8" height="22" rx="3"/>`;
    } else if (p.kind === "passive" || p.kind === "mech" || p.kind === "conn") {
      pads += `<rect class="pad" x="${x}" y="${y}" width="${Math.min(w * 0.3, 20)}" height="${h}"/><rect class="pad" x="${x + w - Math.min(w * 0.3, 20)}" y="${y}" width="${Math.min(w * 0.3, 20)}" height="${h}"/>`;
    }
    const col = PCB.groups[p.grp].color;
    const labelSize = Math.max(9, Math.min(22, Math.min(w, h) * 0.55));
    return `<g class="part" data-ref="${p.ref}">
      ${p.ref.startsWith("H") ? "" : pads + `<rect class="body" x="${x}" y="${y}" width="${w}" height="${h}" fill="${col}"/>`}${p.ref.startsWith("H") ? pads : ""}${p.ref.startsWith("H") ? `<rect class="body" x="${x}" y="${y}" width="${w}" height="${h}" fill="rgba(0,0,0,.15)"/>` : ""}
      <text class="ref" x="${p.ref.startsWith("H") ? x + w + 22 : x + w / 2}" y="${p.ref.startsWith("H") ? y + h / 2 + 6 : y + h / 2 + labelSize * 0.35}" font-size="${p.ref.startsWith("H") ? 16 : labelSize}">${p.ref}</text></g>`;
  }

  function route(r) {
    const n = PCB.nets[r.net];
    const d = r.pts.map((p, i) => `${i ? "L" : "M"}${p[0] * S} ${p[1] * S}`).join(" ");
    if (n.pair) {
      return `<g class="net" data-net="${r.net}"><path d="${d}" stroke="${n.color}" stroke-width="${n.width * S}" transform="translate(-4 0)" fill="none" stroke-linejoin="round"/>
        <path d="${d}" stroke="${n.color}" stroke-width="${n.width * S}" transform="translate(4 0)" fill="none" stroke-linejoin="round"/></g>`;
    }
    return `<path class="net" data-net="${r.net}" d="${d}" stroke="${n.color}" stroke-width="${n.width * S}" fill="none" stroke-linejoin="round" stroke-linecap="round"/>`;
  }

  PCB.renderSvg = function (host, opts) {
    const B = PCB.board, W = B.w * S, Hh = B.h * S;
    const ko = PCB.keepouts.map((k) => k.kind === "circle"
      ? `<circle class="ko" cx="${k.x * S}" cy="${k.y * S}" r="${k.r * S}"/><text class="kotxt" x="${k.x * S}" y="${(k.y + k.r) * S + 14}">${k.label}</text>`
      : `<rect class="ko" x="${k.x * S}" y="${k.y * S}" width="${k.w * S}" height="${k.h * S}"/><text class="kotxt" x="${(k.x + k.w / 2) * S}" y="${(k.y + k.h) * S + 14}">${k.label}</text>`).join("");
    const hd = PCB.head;
    host.innerHTML = `<svg class="pcbsvg" viewBox="-40 -40 ${W + 80} ${Hh + 110}" role="img" aria-label="Hub backpack PCB placement">
      <defs><pattern id="pcbgrid" width="${S * 2}" height="${S * 2}" patternUnits="userSpaceOnUse"><path d="M ${S * 2} 0 L 0 0 0 ${S * 2}" fill="none" stroke="rgba(255,255,255,.05)" stroke-width="1"/></pattern></defs>
      <rect x="0" y="0" width="${W}" height="${Hh}" rx="20" class="board"/>
      <rect x="0" y="0" width="${W}" height="${Hh}" rx="20" fill="url(#pcbgrid)"/>
      <g class="layer-ko">${ko}</g>
      <g class="layer-head"><rect class="head" x="${hd.x * S}" y="${hd.y * S}" width="${hd.w * S}" height="${hd.h * S}"/>
        <text class="headtxt" x="${(hd.x + 1) * S}" y="${(hd.y + 2.2) * S}">HUD HEAD ABOVE (${hd.stack} mm stack)</text></g>
      <g class="layer-cu">${PCB.routes.map(route).join("")}</g>
      <g class="layer-parts">${PCB.parts.map(footprint).join("")}</g>
      <g class="layer-holes">${PCB.holes.map(([x, y]) => `<circle class="hole" cx="${x * S}" cy="${y * S}" r="${1.6 * S}"/><circle class="drill" cx="${x * S}" cy="${y * S}" r="${1.1 * S}"/>`).join("")}</g>
      <g class="dims"><line x1="0" y1="${Hh + 40}" x2="${W}" y2="${Hh + 40}"/><text x="${W / 2}" y="${Hh + 62}">${B.w} mm</text>
        <line x1="${W + 22}" y1="0" x2="${W + 22}" y2="${Hh}"/><text x="${W + 30}" y="${Hh / 2}" transform="rotate(90 ${W + 30} ${Hh / 2})">${B.h} mm</text></g>
      <text class="edge" x="${58 * S}" y="${Hh + 26}">USB-C: CAM · PHONE · PWR IN</text>
    </svg>`;
    host.querySelectorAll(".part").forEach((g) => {
      g.addEventListener("click", () => opts.onSelect(g.dataset.ref));
    });
  };

  PCB.select = function (host, ref) {
    host.querySelectorAll(".part").forEach((g) => g.classList.toggle("sel", g.dataset.ref === ref));
  };

  PCB.setLayer = function (host, layer, on) {
    const g = host.querySelector(".layer-" + layer);
    if (g) g.style.display = on ? "" : "none";
  };

  PCB.info = function (ref) {
    const p = PCB.parts.find((q) => q.ref === ref);
    if (!p) return "";
    return `<b>${esc(p.ref)} · ${esc(p.name)}</b><br><span class="mono">${esc(p.part)}</span> · ${p.w}×${p.h} mm, ${p.z} mm tall · ${PCB.groups[p.grp].label}<br>${esc(p.why)}`;
  };
})();
