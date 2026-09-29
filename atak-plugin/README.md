# PRISM: ATAK plugin

When the TAK HUD is plugged into the phone's USB-C port, this plugin sends
it, as text lines on the HUD's serial console:

- `fix <lat> <lon> <hae>`: the phone's own GPS, once a second
- `trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign>`: every `a-*`
  unit on the map within the chosen range, nearest 40, every 2 s, paced
  20 ms apart so the HUD console never merges lines. Spaces in names go
  as `_`; a unit with no real name (only a UID or coordinates) goes as `-`
  and the HUD shows just its range
- `fake off`: once on connect, so the HUD drops its demo targets

The phone also powers the HUD over the same cable. The HUD's CH343 bridge
enumerates as USB CDC-ACM (VID 1A86 / PID 55D3). The plugin leaves DTR and
RTS de-asserted, because they drive the ESP32's reset/boot circuit.

Target: ATAK-CIV 5.8.0.x (built and tested against SDK 5.8.0.4).

## Build and install

```powershell
# local.properties (not committed):
#   sdk.dir=<Android SDK>
#   atak.sdk.dir=<folder with main.jar and atak-gradle-takdev.jar>
#   sdk.path=<same as atak.sdk.dir>
$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-17.0.17.10-hotspot"
.\gradlew.bat assembleCivDebug
adb install -r app\build\outputs\apk\civ\debug\ATAK-Plugin-PRISM-*.apk
```

In ATAK: **Settings → Tools → Plugins → PRISM → Load**. Tap the PRISM
toolbar icon to see its status.
