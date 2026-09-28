#!/usr/bin/env python3
"""
provision.py - build the HUD's "hudcfg" NVS image: Wi-Fi, TAK server,
ownship UID and the TAK client certificate, without compiling secrets into
the firmware.

TAK Server hands out client identities as PKCS#12 (.p12) files, usually with
a truststore .p12 for the CA (default password "atakatak"). This script
converts them to PEM, writes an NVS CSV and (if available) runs Espressif's
nvs_partition_gen to produce hudcfg.bin. It never flashes anything itself;
it prints the esptool command for you to run.

    python tools/provision.py \
        --ssid MyNet --password secret \
        --tak-host 192.168.1.10 --tak-port 8089 --proto tls \
        --client-p12 HUD-001.p12 --client-pass atakatak \
        --truststore truststore-root.p12 --truststore-pass atakatak \
        --own-uid ANDROID-1234abcd --hud-uid HUD-001

Outputs go to ./provisioning/ (git-ignored). Use a dedicated per-device
identity (HUD-001, HUD-002 ...), never an admin certificate.

Requires: pip install cryptography   (and esp-idf-nvs-partition-gen, or an
ESP-IDF install with IDF_PATH set, to produce the .bin).
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys

PART_SIZE = 0x20000      # must match partitions.csv (hudcfg)
PART_OFFSET = 0x310000
NAMESPACE = "hud"


def load_p12(path: str, password: str | None):
    from cryptography.hazmat.primitives.serialization import pkcs12
    with open(path, "rb") as f:
        data = f.read()
    return pkcs12.load_key_and_certificates(data, password.encode() if password else None)


def pem_cert(cert) -> bytes:
    from cryptography.hazmat.primitives.serialization import Encoding
    return cert.public_bytes(Encoding.PEM)


def pem_key(key) -> bytes:
    from cryptography.hazmat.primitives.serialization import Encoding, NoEncryption, PrivateFormat
    return key.private_bytes(Encoding.PEM, PrivateFormat.TraditionalOpenSSL, NoEncryption())


def f32hex(v: float) -> str:
    return struct.pack("<f", v).hex()


def f64hex(v: float) -> str:
    return struct.pack("<d", v).hex()


def find_nvs_gen():
    try:
        import esp_idf_nvs_partition_gen  # noqa: F401
        return [sys.executable, "-m", "esp_idf_nvs_partition_gen"]
    except ImportError:
        pass
    idf = os.environ.get("IDF_PATH")
    if idf:
        p = os.path.join(idf, "components", "nvs_flash", "nvs_partition_generator", "nvs_partition_gen.py")
        if os.path.exists(p):
            return [sys.executable, p]
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ssid")
    ap.add_argument("--password", default="")
    ap.add_argument("--tak-host")
    ap.add_argument("--tak-port", type=int)
    ap.add_argument("--proto", choices=["off", "tcp", "tls"])
    ap.add_argument("--tak-cn", help="expected server certificate CN if it differs from --tak-host")
    ap.add_argument("--client-p12", help="device identity from TAK Server (.p12)")
    ap.add_argument("--client-pass", default="atakatak")
    ap.add_argument("--truststore", help="CA truststore (.p12) or CA PEM file")
    ap.add_argument("--truststore-pass", default="atakatak")
    ap.add_argument("--own-uid", help="UID of the ATAK phone that supplies own position")
    ap.add_argument("--own-callsign")
    ap.add_argument("--hud-uid", default="HUD-001")
    ap.add_argument("--hfov", type=float)
    ap.add_argument("--vfov", type=float)
    ap.add_argument("--fake", choices=["on", "off"])
    ap.add_argument("--out-dir", default="provisioning")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    rows = [("key", "type", "encoding", "value"), (NAMESPACE, "namespace", "", "")]

    def s(k, v):
        rows.append((k, "data", "string", v))

    if args.ssid is not None:
        s("wifi_ssid", args.ssid)
        s("wifi_pass", args.password)
    if args.tak_host:
        s("tak_host", args.tak_host)
    if args.tak_port:
        rows.append(("tak_port", "data", "u16", str(args.tak_port)))
    if args.proto:
        rows.append(("tak_proto", "data", "u8", str({"off": 0, "tcp": 1, "tls": 2}[args.proto])))
    if args.tak_cn:
        s("tak_cn", args.tak_cn)
    if args.own_uid:
        s("own_uid", args.own_uid)
    if args.own_callsign:
        s("own_cs", args.own_callsign)
    if args.hud_uid:
        s("hud_uid", args.hud_uid)
        s("hud_cs", args.hud_uid)
    if args.hfov:
        rows.append(("hfov", "data", "hex2bin", f32hex(args.hfov)))
    if args.vfov:
        rows.append(("vfov", "data", "hex2bin", f32hex(args.vfov)))
    if args.fake:
        rows.append(("fake", "data", "u8", "1" if args.fake == "on" else "0"))

    if args.client_p12:
        key, cert, extra = load_p12(args.client_p12, args.client_pass)
        cert_path = os.path.join(args.out_dir, "client_cert.pem")
        key_path = os.path.join(args.out_dir, "client_key.pem")
        with open(cert_path, "wb") as f:
            f.write(pem_cert(cert))
        with open(key_path, "wb") as f:
            f.write(pem_key(key))
        rows.append(("cert", "file", "binary", os.path.abspath(cert_path)))
        rows.append(("key", "file", "binary", os.path.abspath(key_path)))
        print(f"client cert: {cert.subject.rfc4514_string()}")

    if args.truststore:
        ca_path = os.path.join(args.out_dir, "ca.pem")
        if args.truststore.lower().endswith((".p12", ".pfx")):
            _, ca_cert, ca_extra = load_p12(args.truststore, args.truststore_pass)
            certs = [c for c in [ca_cert, *(ca_extra or [])] if c is not None]
            with open(ca_path, "wb") as f:
                for c in certs:
                    f.write(pem_cert(c))
        else:
            shutil.copyfile(args.truststore, ca_path)
        rows.append(("ca", "file", "binary", os.path.abspath(ca_path)))

    csv_path = os.path.join(args.out_dir, "hudcfg.csv")
    with open(csv_path, "w", newline="") as f:
        for r in rows:
            f.write(",".join(r) + "\n")
    print(f"wrote {csv_path} ({len(rows) - 2} entries)")

    gen = find_nvs_gen()
    bin_path = os.path.join(args.out_dir, "hudcfg.bin")
    if not gen:
        print("nvs_partition_gen not found: pip install esp-idf-nvs-partition-gen (or set IDF_PATH), then:\n"
              f"  python -m esp_idf_nvs_partition_gen generate {csv_path} {bin_path} {hex(PART_SIZE)}")
        return
    subprocess.check_call(gen + ["generate", csv_path, bin_path, hex(PART_SIZE)])
    print(f"\nwrote {bin_path}. Flash it (this erases previous hudcfg settings) with:\n"
          f"  python -m esptool --chip esp32s3 -p COMx write_flash {hex(PART_OFFSET)} {bin_path}")


if __name__ == "__main__":
    main()
