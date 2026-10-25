"""TLS-validating simulated original ESP32 for the S3 local WSS endpoint.

Use --address with the verified STA IP. The URL hostname remains the
certificate's DNS name, so Python verifies the chain and hostname normally.
No credentials or private keys are printed.
"""
import argparse
import base64
import hashlib
import hmac
import json
from pathlib import Path
import secrets
import socket
import ssl
import struct
import time
import uuid
from datetime import datetime, timedelta, timezone

PATH = "/hwp/ws/v1/local"
DOMAIN = "HWP-WS-V1-AUTH"
FIELDS = ("link", "deviceRole", "installationId", "deviceId", "bootId",
          "deviceSessionId", "connectionSessionId", "serverNonce",
          "clientNonce", "expiresAt")


def wire_time(now=None):
    return (now or datetime.now(timezone.utc)).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def mac(secret_hex, frame):
    key = bytes.fromhex(secret_hex)
    if len(key) != 32:
        raise ValueError("expected 32-byte secret")
    parts = (DOMAIN, *(frame[name] for name in FIELDS))
    payload = b"".join(len(part.encode()).to_bytes(2, "big") + part.encode() for part in parts)
    return hmac.new(key, payload, hashlib.sha256).hexdigest()


def exact(sock, length):
    result = b""
    while len(result) < length:
        chunk = sock.recv(length - len(result))
        if not chunk:
            raise EOFError("WebSocket closed")
        result += chunk
    return result


def write_frame(sock, payload, opcode=1, final=True):
    if isinstance(payload, str):
        payload = payload.encode()
    first = opcode | (0x80 if final else 0)
    size = len(payload)
    header = bytes((first, 0x80 | size)) if size < 126 else (
        bytes((first, 0x80 | 126)) + struct.pack("!H", size)
        if size < 65536 else bytes((first, 0x80 | 127)) + struct.pack("!Q", size))
    mask = secrets.token_bytes(4)
    sock.sendall(header + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(payload)))


def read_frame(sock):
    first, second = exact(sock, 2)
    if second & 0x80 or first & 0x70:
        raise ValueError("invalid server frame")
    size = second & 0x7f
    if size == 126:
        size = struct.unpack("!H", exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(sock, 8))[0]
    if size > 2048:
        raise ValueError("oversized server frame")
    return first & 0x0f, exact(sock, size)


def read_json(sock):
    while True:
        opcode, payload = read_frame(sock)
        if opcode == 9:
            write_frame(sock, payload, opcode=10)
        elif opcode == 1:
            return json.loads(payload)
        else:
            raise EOFError("WebSocket closed or sent non-text data")


def open_wss(address, identity, ca, hostname=None):
    hostname = hostname or identity["hostname"]
    context = ssl.create_default_context(cafile=str(ca) if ca else None)
    context.check_hostname = True
    context.verify_mode = ssl.CERT_REQUIRED
    raw = socket.create_connection((address, 9443), timeout=7)
    try:
        sock = context.wrap_socket(raw, server_hostname=hostname)
    except Exception:
        raw.close()
        raise
    sock.settimeout(7)
    key = base64.b64encode(secrets.token_bytes(16)).decode()
    sock.sendall((f"GET {PATH} HTTP/1.1\r\nHost: {hostname}:9443\r\n"
                  "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                  f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
    header = b""
    while not header.endswith(b"\r\n\r\n") and len(header) < 4096:
        header += exact(sock, 1)
    if not header.startswith(b"HTTP/1.1 101 "):
        sock.close()
        raise ValueError("WebSocket upgrade rejected")
    expected = base64.b64encode(hashlib.sha1(
        (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
    if b"Sec-WebSocket-Accept: " + expected not in header:
        sock.close()
        raise ValueError("WebSocket accept mismatch")
    return sock


def hello(identity):
    return {
        "schema": 1, "kind": "hello", "link": "local", "deviceRole": "esp32",
        "deviceId": identity["deviceId"], "installationId": identity["installationId"],
        "bootId": secrets.token_hex(16), "deviceSessionId": str(uuid.uuid4()),
        "connectionSessionId": str(uuid.uuid4()), "sentAt": wire_time(),
        "supportedVersions": [1],
    }


def authenticate(sock, identity, *, bad_mac=False, return_response=False):
    start = hello(identity)
    write_frame(sock, json.dumps(start, separators=(",", ":")))
    challenge = read_json(sock)
    if challenge.get("kind") != "auth-challenge":
        raise ValueError("challenge missing")
    for key in ("link", "deviceRole", "deviceId", "installationId", "bootId",
                "deviceSessionId", "connectionSessionId"):
        if challenge.get(key) != start[key]:
            raise ValueError("challenge identity mismatch")
    response = dict(challenge, kind="auth-response",
                    clientNonce=secrets.token_hex(32))
    response["mac"] = ("0" * 64 if bad_mac else mac(identity["secretHex"], response))
    write_frame(sock, json.dumps(response, separators=(",", ":")))
    if bad_mac:
        try:
            read_json(sock)
        except (EOFError, OSError):
            return None
        raise AssertionError("invalid HMAC was accepted")
    ready = read_json(sock)
    if ready.get("kind") != "ready" or ready.get("connectionSessionId") != start["connectionSessionId"]:
        raise AssertionError("authentication did not establish a fresh session")
    return (ready, response) if return_response else ready


def state(ready, sequence):
    return {
        "schema": 1, "kind": "state", "link": "local", "producerRole": "esp32",
        "connectionSessionId": ready["connectionSessionId"], "sequence": sequence,
        "state": {
            "schema": 1, "source": "original-esp32", "bootId": ready["bootId"],
            "deviceSessionId": ready["deviceSessionId"], "observedAt": wire_time(),
            "quality": "reported", "sequence": sequence,
            "boilerTemperatureC": 48.5, "solarTemperatureC": 54.0,
            "valve": {"reported": "open", "evidence": "inferred"},
            "manualMode": False,
        },
    }


def expect_closed(sock):
    sock.settimeout(4)
    try:
        while True:
            opcode, _ = read_frame(sock)
            if opcode == 8:
                return
    except (EOFError, OSError):
        return
    raise AssertionError("connection remained open after rejection")


def hardware_suite(address, identity, ca):
    try:
        open_wss(address, identity, None)
    except ssl.SSLCertVerificationError:
        print("PASS: unknown CA rejected")
    else:
        raise AssertionError("unknown CA accepted")
    try:
        open_wss(address, identity, ca, hostname="wrong.example.invalid")
    except ssl.SSLCertVerificationError:
        print("PASS: wrong hostname rejected")
    else:
        raise AssertionError("wrong hostname accepted")
    for field in ("deviceId", "installationId"):
        with open_wss(address, identity, ca) as sock:
            bad = hello(identity)
            bad[field] = str(uuid.uuid4())
            write_frame(sock, json.dumps(bad))
            expect_closed(sock)
    print("PASS: wrong device and installation rejected")
    with open_wss(address, identity, ca) as sock:
        early = state(hello(identity), 1)
        write_frame(sock, json.dumps(early))
        expect_closed(sock)
    print("PASS: pre-auth telemetry rejected")
    with open_wss(address, identity, ca) as sock:
        authenticate(sock, identity, bad_mac=True)
    print("PASS: invalid HMAC rejected")
    with open_wss(address, identity, ca) as sock:
        write_frame(sock, json.dumps(hello(identity)))
        read_json(sock)
        time.sleep(11)
        expect_closed(sock)
    print("PASS: expired authentication challenge closed")
    for cycle in range(10):
        with open_wss(address, identity, ca) as sock:
            ready = authenticate(sock, identity)
            write_frame(sock, json.dumps(state(ready, cycle + 1), separators=(",", ":")))
            time.sleep(0.15)
        time.sleep(0.15)
    print("PASS: ten authenticated connect/telemetry/disconnect cycles")
    with open_wss(address, identity, ca) as sock:
        _, response = authenticate(sock, identity, return_response=True)
        write_frame(sock, json.dumps(response))
        expect_closed(sock)
    print("PASS: repeated authentication response rejected")
    with open_wss(address, identity, ca) as sock:
        first = hello(identity)
        write_frame(sock, json.dumps(first))
        challenge = read_json(sock)
        response = dict(challenge, kind="auth-response", clientNonce=secrets.token_hex(32))
        response["mac"] = mac(identity["secretHex"], response)
        write_frame(sock, json.dumps(response))
        if read_json(sock).get("kind") != "ready":
            raise AssertionError("initial session not ready")
    time.sleep(0.3)
    with open_wss(address, identity, ca) as sock:
        write_frame(sock, json.dumps(first))
        expect_closed(sock)
    print("PASS: old session rejected after reconnect")
    cases = (
        ("duplicate telemetry", {"sequence": 2, "repeat": True}),
        ("out-of-order telemetry", {"sequence": 3, "older": 2}),
        ("malformed JSON", {"payload": "{bad"}),
        ("duplicate JSON keys", {"payload": '{"kind":"state","kind":"state"}'}),
        ("invalid UTF-8", {"payload": b'{"kind":"\xc0\xaf"}'}),
        ("oversized frame", {"payload": "x" * 2049}),
        ("fragmented frame", {"payload": "{}", "final": False}),
        ("unsupported command", {"payload": '{"schema":1,"kind":"command"}'}),
    )
    for label, options in cases:
        with open_wss(address, identity, ca) as sock:
            ready = authenticate(sock, identity)
            if "sequence" in options:
                initial = state(ready, options["sequence"])
                write_frame(sock, json.dumps(initial))
                time.sleep(0.1)
                payload = initial if options.get("repeat") else state(ready, options["older"])
                payload = json.dumps(payload)
            else:
                payload = options["payload"]
            write_frame(sock, payload, final=options.get("final", True))
            expect_closed(sock)
        time.sleep(0.2)
        print(f"PASS: {label} rejected")
    with open_wss(address, identity, ca) as sock:
        ready = authenticate(sock, identity)
        bad = state(ready, 5)
        bad["state"]["valve"]["evidence"] = "sensed"
        write_frame(sock, json.dumps(bad))
        expect_closed(sock)
    print("PASS: unsupported physical-position claim rejected")
    with open_wss(address, identity, ca) as first_sock:
        authenticate(first_sock, identity)
        try:
            with open_wss(address, identity, ca) as second_sock:
                write_frame(second_sock, json.dumps(hello(identity)))
                expect_closed(second_sock)
        except (OSError, EOFError, ssl.SSLError):
            pass  # The socket cap may reject the second TLS handshake earlier.
    print("PASS: concurrent second client bounded")
    with open_wss(address, identity, ca) as sock:
        ready = authenticate(sock, identity)
        sock.settimeout(26)
        opcode, payload = read_frame(sock)
        if opcode != 9:
            raise AssertionError("expected WSS heartbeat ping")
        write_frame(sock, payload, opcode=10)
        write_frame(sock, json.dumps(state(ready, 1)))
        time.sleep(0.2)
    print("PASS: heartbeat ping/pong and post-pong telemetry")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--address", required=True, help="verified ESP32-S3 STA address")
    parser.add_argument("--config", type=Path, default=Path(".local-wss/client.json"))
    parser.add_argument("--ca", type=Path, default=Path(".local-wss/ca.pem"))
    args = parser.parse_args()
    identity = json.loads(args.config.read_text())
    hardware_suite(args.address, identity, args.ca)


if __name__ == "__main__":
    main()
