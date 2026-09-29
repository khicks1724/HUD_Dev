# ATAK phone over USB-C: power and tracks on one cable

**Power** works today. The board's USB-C has 5.1 kΩ CC pull-downs, so an
Android phone treats it as a USB device and supplies 5 V (OTG). The HUD
draws about 150-350 mA. Use a C-to-C data cable, and on some phones enable
"USB controlled by: this device" or OTG.

**Tracks** need a small ATAK plugin, because the HUD's USB is a CH343
USB-serial bridge, not a network interface.

## HUD side (implemented)

Console commands on the same serial port:
```
fix <lat> <lon> <hae>                                   # phone GPS = own position (source "USB")
trk <uid> <type> <lat> <lon> <hae> <stale_s> <callsign> # one track
hdg <deg> <gain>                                        # phone compass nudge (gain 0.02-0.1)
```
The console echoes input and prints a prompt; the plugin should ignore
anything it reads back.

## Plugin side (to build)

- Start from the ATAK plugin template (Kyle's `Test Coding/ATAK Plugin`
  folder may already have one set up).
- USB serial: `usb-serial-for-android` (supports CH34x). Open at 2,000,000 8N1 (the HUD switches its USB link to 2 Mbit/s after boot)
  and request USB permission on attach (`USB_DEVICE_ATTACHED` intent filter
  with the CH343 VID/PID 0x1A86/0x55D3).
- Every 1 s, send `fix` from `MapView.getSelfMarker()`.
- On every CoT event ATAK dispatches (a `CotServiceRemote` /
  `CommsMapComponent` listener, or iterate map items in the "Cursor on
  Target" group), send `trk` for atoms (`a-*`) within, say, 10 km.
- Optional: send the phone's compass as `hdg <deg> 0.05` when the phone is
  mounted facing the same way as the HUD (e.g. on a chest rig). Otherwise
  leave heading to the HUD.

The result: the phone already has the whole picture (TAK Server, mesh,
other plugins), certificates stay on the phone, and the HUD needs no Wi-Fi.
