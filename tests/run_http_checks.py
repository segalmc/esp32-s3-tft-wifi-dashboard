#!/usr/bin/env python3
"""Run the required HTTP host checks and fail if any are missing."""
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
TESTS = (
    "http_telemetry_record_test.cpp",
    "http_telemetry_wire_test.cpp",
    "http_telemetry_journal_test.cpp",
    "http_telemetry_acceptance_test.cpp",
    "http_telemetry_freshness_test.cpp",
)
with tempfile.TemporaryDirectory(prefix="hwp-s3-http-check-") as directory:
    for name in TESTS:
        source = HERE / name
        if not source.is_file():
            raise SystemExit(f"missing expected test: {name}")
        binary = Path(directory) / source.stem
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"PASS {name}", flush=True)
