#!/usr/bin/env python3
"""
sim_server.py - a stand-in TAK Server + scenario generator + local web host.

Standard library only. One process gives you:

  * CoT over TCP  (default :8087)  - point the HUD at it with
        tak <this-pc-ip> 8087 tcp
  * CoT over TLS  (optional :8089) - --tls-cert/--tls-key [--tls-ca to demand
        client certs], to rehearse the real TAK Server mutual-TLS setup
  * ATAK-style SA multicast        - --mesh xml|proto (239.2.3.1:6969)
  * The web page on http://localhost:8000/ with a live scenario feed at
        /api/sim/state (JSON) and /api/sim/stream (Server-Sent Events)

The scenario (friendly patrol, vehicle, UAV orbit, hostile, neutral, unknown,
plus an "owner phone") is the same one the firmware's fake mode and the web
simulator use, so all three views can be compared side by side.

    python tools/sim_server.py                      # TCP 8087 + web 8000
    python tools/sim_server.py --lat 36.6 --lon -121.9 --mesh xml
    python tools/sim_server.py --hud 192.168.1.50   # also push POS/HDG to the HUD
"""
from __future__ import annotations

import argparse
import datetime as dt
import functools
import http.server
import json
import math
import os
import socket
import socketserver
import ssl
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hudmath as hm  # noqa: E402
import takproto  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")
OWNER_UID = "ANDROID-SIM-OWNER"


# ---------------------------------------------------------------- scenario

class Scenario:
    def __init__(self, lat: float, lon: float, hae: float = 20.0):
        self.origin = (lat, lon, hae)
        self.t0 = time.time()

    def units(self, t: float | None = None):
        """Return list of dicts; mirrors firmware/main/fake_targets.c."""
        if t is None:
            t = time.time() - self.t0
        o = self.origin
        out = []

        def add(uid, cs, typ, e, n, u, course, speed, team="Cyan"):
            lat, lon, hae = hm.enu_offset_to_lla(o, e, n, u)
            out.append({"uid": uid, "cs": cs, "type": typ, "lat": lat, "lon": lon, "hae": hae,
                        "course": course % 360.0, "speed": speed, "team": team})

        a = t * 1.4 / 150.0
        add("SIM-ALPHA1", "ALPHA1", "a-f-G-U-C-I", 150 * math.sin(a), 350 + 150 * math.cos(a), 0,
            math.degrees(a) + 90, 1.4)
        x = -385 + 300 * math.sin(t / 40.0)
        add("SIM-BRAVO3", "BRAVO3", "a-f-G-E-V", x, 460, 0, 90 if math.cos(t / 40.0) > 0 else 270, 7.5)
        b = t * 25.0 / 600.0
        add("SIM-UAV12", "UAV12", "a-f-A-M-F-Q", 1100 + 600 * math.cos(b), 1100 + 600 * math.sin(b), 450,
            -math.degrees(b), 25.0)
        add("SIM-H1", "TGT-H1", "a-h-G-U-C", 60, 1400, 8, 0, 0, team="Red")
        add("SIM-N1", "CIV", "a-n-G", -900, 300, 0, 0, 0, team="White")
        add("SIM-U1", "UNK", "a-u-G", 900, -700, 0, 0, 0, team="Yellow")
        # The owner's phone: slow 20 m wander around the origin.
        add(OWNER_UID, "KYLE", "a-f-G-U-C", 20 * math.sin(t / 30), 20 * math.cos(t / 45), 0,
            math.degrees(math.atan2(math.cos(t / 30) / 30, -math.sin(t / 45) / 45)), 0.8)
        return out


def iso(ts: float) -> str:
    return dt.datetime.fromtimestamp(ts, dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.") + \
        f"{int((ts % 1) * 1000):03d}Z"


def xml_escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def cot_xml(u: dict, stale_s: int = 10) -> bytes:
    now = time.time()
    return (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        f'<event version="2.0" uid="{xml_escape(u["uid"])}" type="{u["type"]}" how="m-g" '
        f'time="{iso(now)}" start="{iso(now)}" stale="{iso(now + stale_s)}">'
        f'<point lat="{u["lat"]:.7f}" lon="{u["lon"]:.7f}" hae="{u["hae"]:.1f}" ce="5.0" le="9999999.0"/>'
        f'<detail><contact callsign="{xml_escape(u["cs"])}"/>'
        f'<__group name="{u["team"]}" role="Team Member"/>'
        f'<track course="{u["course"]:.1f}" speed="{u["speed"]:.2f}"/></detail></event>'
    ).encode()


def cot_proto(u: dict, stale_s: int = 10) -> bytes:
    now_ms = int(time.time() * 1000)
    return takproto.encode_mesh(uid=u["uid"], cot_type=u["type"], lat=u["lat"], lon=u["lon"], hae=u["hae"],
                                callsign=u["cs"], team=u["team"], course=u["course"], speed=u["speed"],
                                time_ms=now_ms, stale_ms=now_ms + stale_s * 1000)


# ---------------------------------------------------------------- CoT servers

class CotHub:
    """Tracks connected streaming clients and broadcasts to them."""

    def __init__(self):
        self.clients: list[socket.socket] = []
        self.lock = threading.Lock()
        self.rx_events = 0

    def add(self, s):
        with self.lock:
            self.clients.append(s)

    def broadcast(self, data: bytes):
        with self.lock:
            dead = []
            for c in self.clients:
                try:
                    c.sendall(data)
                except OSError:
                    dead.append(c)
            for c in dead:
                self.clients.remove(c)
                try:
                    c.close()
                except OSError:
                    pass

    def count(self):
        with self.lock:
            return len(self.clients)


def serve_cot(hub: CotHub, port: int, tls_ctx: ssl.SSLContext | None):
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", port))
    srv.listen(8)
    kind = "TLS" if tls_ctx else "TCP"
    print(f"[cot] {kind} streaming on :{port}")
    while True:
        conn, addr = srv.accept()
        try:
            if tls_ctx:
                conn = tls_ctx.wrap_socket(conn, server_side=True)
                peer = conn.getpeercert()
                who = dict(x[0] for x in peer.get("subject", ())) if peer else {}
                print(f"[cot] TLS client {addr[0]} cert CN={who.get('commonName', '-')}")
            else:
                print(f"[cot] TCP client {addr[0]}")
        except (ssl.SSLError, OSError) as e:
            print(f"[cot] handshake from {addr[0]} failed: {e}")
            conn.close()
            continue
        hub.add(conn)
        threading.Thread(target=drain, args=(hub, conn, addr), daemon=True).start()


def drain(hub: CotHub, conn, addr):
    """Read (and count) what the client sends: pings, its own SA."""
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            hub.rx_events += data.count(b"</event>")
    except OSError:
        pass
    print(f"[cot] client {addr[0]} disconnected")


# ---------------------------------------------------------------- web

class State:
    def __init__(self, sc: Scenario, hub: CotHub):
        self.sc = sc
        self.hub = hub

    def snapshot(self):
        units = self.sc.units()
        return {"t": time.time(), "origin": self.sc.origin, "owner_uid": OWNER_UID,
                "clients": self.hub.count(), "rx_events": self.hub.rx_events, "units": units}


def make_handler(state: State):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):  # quieter
            if "/api/" not in (args[0] if args else ""):
                super().log_message(fmt, *args)

        def end_headers(self):
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Cache-Control", "no-store")
            super().end_headers()

        def do_GET(self):
            if self.path.startswith("/api/sim/state"):
                body = json.dumps(state.snapshot()).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path.startswith("/api/sim/stream"):
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.end_headers()
                try:
                    while True:
                        self.wfile.write(f"data: {json.dumps(state.snapshot())}\n\n".encode())
                        self.wfile.flush()
                        time.sleep(0.2)
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                    return
            return super().do_GET()

    return Handler


class ThreadingHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


# ---------------------------------------------------------------- main

def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lat", type=float, default=36.5967)
    ap.add_argument("--lon", type=float, default=-121.8750)
    ap.add_argument("--hae", type=float, default=20.0)
    ap.add_argument("--tcp-port", type=int, default=8087)
    ap.add_argument("--tls-port", type=int, default=8089)
    ap.add_argument("--tls-cert", help="server certificate (PEM) to enable TLS")
    ap.add_argument("--tls-key", help="server private key (PEM)")
    ap.add_argument("--tls-ca", help="CA (PEM) that client certificates must chain to")
    ap.add_argument("--mesh", choices=["xml", "proto"], help="also send SA multicast to 239.2.3.1:6969")
    ap.add_argument("--hud", help="HUD IP: also send POS (owner phone) over UDP 4349")
    ap.add_argument("--web-port", type=int, default=8000)
    ap.add_argument("--rate", type=float, default=1.0, help="CoT updates per second")
    args = ap.parse_args()

    sc = Scenario(args.lat, args.lon, args.hae)
    hub = CotHub()
    threading.Thread(target=serve_cot, args=(hub, args.tcp_port, None), daemon=True).start()
    if args.tls_cert and args.tls_key:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.tls_cert, args.tls_key)
        if args.tls_ca:
            ctx.load_verify_locations(args.tls_ca)
            ctx.verify_mode = ssl.CERT_REQUIRED
        threading.Thread(target=serve_cot, args=(hub, args.tls_port, ctx), daemon=True).start()

    handler = make_handler(State(sc, hub))
    handler = functools.partial(handler, directory=WEB_DIR)
    httpd = ThreadingHTTPServer(("0.0.0.0", args.web_port), handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()

    mcast = None
    if args.mesh:
        mcast = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        mcast.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if args.hud else None

    ip = local_ip()
    print(f"[web] http://localhost:{args.web_port}/  (LAN: http://{ip}:{args.web_port}/)")
    print(f"[hud] on the HUD console:  tak {ip} {args.tcp_port} tcp   then   own uid {OWNER_UID}")
    try:
        while True:
            for u in sc.units():
                hub.broadcast(cot_xml(u))
                if mcast:
                    pkt = cot_xml(u) if args.mesh == "xml" else cot_proto(u)
                    mcast.sendto(pkt, ("239.2.3.1", 6969))
                if udp and u["uid"] == OWNER_UID:
                    udp.sendto(f"POS,{u['lat']:.7f},{u['lon']:.7f},{u['hae']:.1f}".encode(), (args.hud, 4349))
            time.sleep(1.0 / args.rate)
    except KeyboardInterrupt:
        print("bye")


if __name__ == "__main__":
    main()
