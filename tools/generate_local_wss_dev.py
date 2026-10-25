"""Generate ignored, short-lived S3 WSS development credentials.

Never use these as production credentials. Requires OpenSSL on the host.
"""
from pathlib import Path
import argparse
import json
import os
import re
import secrets
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".local-wss"
HEADER = ROOT / "include" / "LocalWssConfig.h"


def run(*args):
    subprocess.run(["openssl", *map(str, args)], check=True, cwd=OUT,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hostname", default="natureglass.local",
                        help="locally resolvable certificate DNS name; may be the existing OTA .local hostname")
    args = parser.parse_args()
    hostname = args.hostname
    if not re.fullmatch(r"[a-z0-9][a-z0-9.-]{1,119}\.local", hostname) or ".." in hostname:
        raise SystemExit("hostname must be a valid lowercase .local DNS name")
    if HEADER.exists() or OUT.exists():
        raise SystemExit("Existing local WSS credentials found; refusing to replace them")
    OUT.mkdir(mode=0o700)
    run("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", "ca.key")
    run("req", "-x509", "-new", "-sha256", "-key", "ca.key", "-days", "30",
        "-subj", "/CN=Nature Glass local development CA",
        "-addext", "basicConstraints=critical,CA:TRUE",
        "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        "-out", "ca.pem")
    run("genpkey", "-algorithm", "EC", "-pkeyopt", "ec_paramgen_curve:P-256", "-out", "server.key")
    run("req", "-new", "-sha256", "-key", "server.key", "-subj", f"/CN={hostname}",
        "-out", "server.csr")
    extensions = OUT / "extensions.cnf"
    extensions.write_text(
        f"subjectAltName=DNS:{hostname}\n"
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature\n"
        "extendedKeyUsage=serverAuth\n"
    )
    run("x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key",
        "-CAcreateserial", "-days", "14", "-sha256", "-extfile", extensions,
        "-out", "server.pem")
    identity = {
        "hostname": hostname,
        "deviceId": str(uuid.uuid4()),
        "installationId": str(uuid.uuid4()),
        "secretHex": secrets.token_hex(32),
    }
    (OUT / "client.json").write_text(json.dumps(identity))
    certificate = (OUT / "server.pem").read_text()
    private_key = (OUT / "server.key").read_text()
    ca_certificate = (OUT / "ca.pem").read_text()
    HEADER.write_text(
        "#pragma once\n"
        f'#define LOCAL_WSS_HOSTNAME "{hostname}"\n'
        "#define LOCAL_WSS_PORT 9443\n"
        f'#define LOCAL_WSS_DEVICE_ID "{identity["deviceId"]}"\n'
        f'#define LOCAL_WSS_INSTALLATION_ID "{identity["installationId"]}"\n'
        f'#define LOCAL_WSS_SECRET_HEX "{identity["secretHex"]}"\n'
        f'#define LOCAL_WSS_CERT_PEM R"PEM({certificate})PEM"\n'
        f'#define LOCAL_WSS_KEY_PEM R"PEM({private_key})PEM"\n'
        f'#define LOCAL_WSS_CA_PEM R"PEM({ca_certificate})PEM"\n'
    )
    for path in (OUT / "ca.key", OUT / "server.key", OUT / "client.json", HEADER):
        os.chmod(path, 0o600)
    print("Ignored development WSS credentials created. Trust .local-wss/ca.pem only for this test.")


if __name__ == "__main__":
    main()
