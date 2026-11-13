#!/usr/bin/env python3
"""Run the required HTTP host checks and fail if any are missing."""
from pathlib import Path
import ctypes.util
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
MBEDCRYPTO = ctypes.util.find_library("mbedcrypto")
TESTS = (
    "http_telemetry_record_test.cpp",
    "http_telemetry_wire_test.cpp",
    "http_telemetry_auth_test.cpp",
    "http_telemetry_transport_test.cpp",
    "http_telemetry_protocol_test.cpp",
    "http_telemetry_journal_test.cpp",
    "http_telemetry_acceptance_test.cpp",
    "http_telemetry_integration_test.cpp",
    "http_telemetry_freshness_test.cpp",
    "now_telemetry_protocol_test.cpp",
    "controller_link_status_test.cpp",
    "original_telemetry_api_test.cpp",
    "now_replay_guard_test.cpp",
    "now_discovery_exchange_test.cpp",
)
with tempfile.TemporaryDirectory(prefix="hwp-s3-http-check-") as directory:
    for name in TESTS:
        source = HERE / name
        if not source.is_file():
            raise SystemExit(f"missing expected test: {name}")
        binary = Path(directory) / source.stem
        command = ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror"]
        crypto = name in {"http_telemetry_auth_test.cpp", "http_telemetry_protocol_test.cpp",
                          "http_telemetry_integration_test.cpp"}
        if crypto:
            headers = next((Path.home() / ".platformio/packages").glob(
                "framework-arduinoespressif32-esp32s3-libs/include/mbedtls/mbedtls/include"), None)
            if not headers or not MBEDCRYPTO:
                raise SystemExit("missing host mbedcrypto library or PlatformIO headers")
            command += [f"-I{headers}"]
        command += [str(source), "-o", str(binary)]
        if name == "original_telemetry_api_test.cpp":
            arduino = next((HERE.parent / '.pio/libdeps').glob('*/ArduinoJson/src'), None)
            if arduino is None:
                raise SystemExit('build an S3 environment to provide ArduinoJson headers')
            command.insert(4, f'-I{arduino}')
        if crypto:
            command += [f"-Wl,-l:{MBEDCRYPTO}"]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
        print(f"PASS {name}", flush=True)

subprocess.run(["python3", str(HERE.parent / "tools/test_usb_serial_latency.py")], check=True)
subprocess.run(["python3", str(HERE.parent / "tools/test_telemetry_snapshot_order.py")], check=True)
