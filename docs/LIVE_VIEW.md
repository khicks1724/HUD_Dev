# Live view: see the HUD in the browser

The **Live HUD** tab of the project page shows what the HUD is displaying,
either over USB-C or over Wi-Fi.

## USB-C (Web Serial)

1. Plug the HUD into the PC. Close `idf.py monitor`, since only one program
   can hold the COM port.
2. Open the page in **Chrome or Edge**, either from file:// or
   http://localhost:8000. Web Serial needs a secure context, and both of
   those count.
3. **Connect USB-C** → pick the CH343 port. The page sends `stream on 5` and
   the HUD starts printing lines like:
   ```
   @HUD {"t":123,"att":{"h":8.1,"p":2.0,"r":0.4,"q":[...],"src":"GYRO"},"own":{...},"targets":[{"cs":"ALPHA1","a":1,"d":1,"e":[120.3,321.9,2.0],"age":0.4}, ...]}
   ```
4. The page redraws the HUD from this state with `hud_math.js`, the JS port
   of the firmware renderer, so it looks the same as the prism. The console
   box sends any command (`status`, `mode 2`, `tak ...`).

115200 baud is ~11 kB/s, which is enough for state at 5-10 Hz with 20
targets but not for raw frames. (The CH343 can go much faster if we ever
want frames over USB.)

## Wi-Fi (HTTP)

Enter the HUD's IP (shown by `status`, or on the STATUS screen) and click
**Connect Wi-Fi**. The page polls:

| Endpoint | Returns |
|---|---|
| `GET /api/state` | the same JSON as the serial line (up to 64 targets) |
| `GET /api/frame` | the live framebuffer, 240×240 RGB565 big-endian (115 200 bytes) |
| `GET /api/mode?m=0..3` | switch display mode |
| `GET /api/thermal` | cycle the thermal underlay |
| `GET /api/hdg?deg=N` | set true heading |

Responses carry `Access-Control-Allow-Origin: *`, so the page works from
file://. `/api/frame` is the exact image the LCD shows before mirroring.

## Demo mode

**Demo (no hardware)** pushes the built-in simulator through the same JSON
path, which is useful to check the page before the board is flashed.

## Security note

The HTTP API has no authentication. It's meant for a bench or a trusted
network. Turn it off or add a token before taking the HUD onto a shared
network (docs/SECURITY.md).
