# PRISM for WinTAK

WinTAK 5.8 plugin that does for a laptop what the ATAK PRISM plugin does for
the phone, plus live thermal:

- **Tracks**: every WinTAK unit (`a-*`) within the chosen range, nearest 40,
  every 2 s, and WinTAK's own position once a second (`trk` / `fix` lines,
  same as ATAK).
- **Thermal**: an RPX UAV640 camera on another USB port. PRISM runs its 8-bit
  white-hot equalized stream (RPX's demo protocol, see
  `digital-trident-ir-camera-suite/RPX`), shrinks 640x480 to 160x120 and sends
  ~8-10 fps to the HUD with the chosen colour palette. The HUD places the
  image from the camera FOV (32° for the C lens, divided by zoom), so heat
  lines up with the world.
- **Controls**: display mode, layout presets and on-screen toggles, range,
  thermal Off/Full/Hot, palette (White hot, Black hot, Ironbow, Rainbow,
  Red hot, Arctic, Green), camera FOV, zoom, contrast enhance, thermal
  alignment sliders, level trim, IMU flip, brightness.

Both devices are found by USB ID (HUD CH343 `1A86:55D3`, RPX `0483:5740` or
`0483:A499`); pick a COM port in the pane to override. The HUD link runs at
2 Mbit/s (`firmware/main/usb_link.h`).

## Build and install

Needs WinTAK 5.8.0.147 with its SDK (NuGet packages in
`C:\Program Files\WinTAK\NuGet`) and Visual Studio 2022.

```powershell
.\scripts\deploy.ps1                 # build Release, install for this user
.\scripts\deploy.ps1 -RestartWinTak  # same, closing and reopening WinTAK
```

Installs to `%APPDATA%\WinTAK\Plugins\PrismHud.WinTAK` and registers it in
`%APPDATA%\WinTAK\Plugins.xml`. Open it from **Plugins → PRISM**.

Settings live in `%APPDATA%\WinTAK\PrismHud\settings.json`. If a unit on the
map doesn't reach the HUD, `%APPDATA%\WinTAK\PrismHud\map-items.txt` lists
what WinTAK's map holds.

No camera? **Thermal → Test pattern** sends a moving hot spot and a warm
post at the boresight, for checking the link and alignment.
`tools/thermal_feed.py COM11` does the same from Python without WinTAK.
