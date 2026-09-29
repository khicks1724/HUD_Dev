"""Send a synthetic thermal stream to the HUD over USB (firmware/main/usb_link.h).

Tests the link without WinTAK or a camera: a 160x120 moving hot-spot pattern
with a palette, as the WinTAK plugin would send from an RPX camera.

  python tools/thermal_feed.py COM11 [--fov 32] [--fps 10] [--palette iron] [--secs 10]
"""
import argparse
import json
import math
import struct
import time

import serial

MAGIC = b"\xA5\x5A"


def packet(ptype, payload=b"", w=0, h=0, hfov=0.0):
    hdr = struct.pack("<BBHHHI", ptype, 0, w, h, int(round(hfov * 100)), len(payload))
    body = hdr + payload
    return MAGIC + body + struct.pack("<H", sum(body) & 0xFFFF)


def palette(name):
    out = bytearray()
    for i in range(256):
        t = i / 255.0
        if name == "white":
            r = g = b = i
        elif name == "iron":
            r = min(1.0, t * 1.9)
            g = max(0.0, min(1.0, t * 1.7 - 0.65))
            b = t * 2.6 if t < 0.3 else max(0.0, 0.78 - (t - 0.3) * 2.6)
            if t > 0.88:
                b = (t - 0.88) * 8
            r, g, b = (int(255 * min(1.0, v)) for v in (r, g, b))
        else:  # green night-vision style
            r, g, b = int(60 * t), i, int(60 * t)
        out += bytes((r, g, b))
    return bytes(out)


def frame(w, h, t):
    px = bytearray(w * h)
    cx, cy = w * (0.5 + 0.3 * math.sin(t * 0.8)), h * 0.55
    for y in range(h):
        base = 25 + y // 4 if y < h // 2 else 60 + (y - h // 2) // 2
        row = y * w
        for x in range(w):
            d = (x - cx) ** 2 + ((y - cy) * 2) ** 2
            px[row + x] = 245 - int(d / 3) if d < 300 else base
    return bytes(px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--fov", type=float, default=32.0)
    ap.add_argument("--fps", type=float, default=10.0)
    ap.add_argument("--palette", default="iron")
    ap.add_argument("--secs", type=float, default=10.0)
    ap.add_argument("--size", default="160x120")
    a = ap.parse_args()
    w, h = (int(v) for v in a.size.split("x"))
    # DTR/RTS drive the ESP32 reset circuit: keep them released before opening
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port, 2000000, 0
    s.dtr = False
    s.rts = False
    s.open()
    s.write(b"\nstream on 2\nthermal 1\n")
    s.write(packet(2, palette(a.palette)))
    t0 = time.time()
    n = 0
    rx = b""
    last = None
    while time.time() - t0 < a.secs:
        s.write(packet(1, frame(w, h, time.time() - t0), w, h, a.fov))
        n += 1
        rx += s.read(65536)
        *lines, rx = rx.split(b"\n")
        for ln in lines:
            if ln.startswith(b"@HUD "):
                last = json.loads(ln[5:])
        time.sleep(max(0.0, t0 + n / a.fps - time.time()))
    s.write(b"stream off\n")
    s.close()
    print(f"sent {n} frames in {time.time() - t0:.1f}s")
    if last:
        print(f"HUD reports thermal source={last.get('thsrc')} fps={last.get('thfps')} mode={last.get('thermal')}")


if __name__ == "__main__":
    main()
