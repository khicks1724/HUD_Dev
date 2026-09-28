"""Minimal TAK Protocol v1 (protobuf) encoder, no dependencies.

Encodes just the fields the HUD decodes (see
firmware/components/hud_core/src/hud_takproto.c). Used by sim_server.py
--mesh-proto and to generate the C unit-test fixture.
"""
import struct


def _varint(v: int) -> bytes:
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def _key(field: int, wire: int) -> bytes:
    return _varint((field << 3) | wire)


def _str(field: int, s: str) -> bytes:
    b = s.encode()
    return _key(field, 2) + _varint(len(b)) + b


def _msg(field: int, body: bytes) -> bytes:
    return _key(field, 2) + _varint(len(body)) + body


def _dbl(field: int, v: float) -> bytes:
    return _key(field, 1) + struct.pack("<d", v)


def _u64(field: int, v: int) -> bytes:
    return _key(field, 0) + _varint(v)


def encode_event(uid, cot_type, lat, lon, hae, callsign=None, team=None, course=None, speed=None,
                 time_ms=0, stale_ms=0, how="m-g") -> bytes:
    detail = b""
    if callsign:
        detail += _msg(2, _str(1, "*:-1:stcp") + _str(2, callsign))
    if team:
        detail += _msg(3, _str(1, team) + _str(2, "Team Member"))
    if course is not None or speed is not None:
        detail += _msg(7, _dbl(1, speed or 0.0) + _dbl(2, course or 0.0))
    ev = (_str(1, cot_type) + _str(5, uid) + _u64(6, time_ms) + _u64(7, time_ms)
          + _u64(8, stale_ms) + _str(9, how) + _dbl(10, lat) + _dbl(11, lon) + _dbl(12, hae)
          + _dbl(13, 5.0) + _dbl(14, 9999999.0) + _msg(15, detail))
    return _msg(2, ev)


def encode_mesh(**kw) -> bytes:
    return b"\xbf\x01\xbf" + encode_event(**kw)


if __name__ == "__main__":
    data = encode_mesh(uid="ANDROID-proto1", cot_type="a-h-G-U-C", lat=36.6, lon=-121.9, hae=12.5,
                       callsign="HOSTILE 7", team="Red", course=45.0, speed=2.5,
                       time_ms=1790606649000, stale_ms=1790606709000)
    print(", ".join(f"0x{b:02x}" for b in data))
