#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PASS=0
FAIL=0

step() {
    echo
    echo "=================================================="
    echo " $1"
    echo "=================================================="
}

run_check() {
    local name="$1"
    shift

    echo
    echo ">>> $name"

    if "$@"; then
        echo "[PASS] $name"
        ((PASS+=1))
    else
        echo "[FAIL] $name"
        ((FAIL+=1))
        return 1
    fi
}

# Locate PlatformIO
if command -v pio >/dev/null 2>&1; then
    PIO="$(command -v pio)"
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "ERROR: PlatformIO not found."
    exit 1
fi

step "S3 TELEMETRY VALIDATION"

echo "Repository: $ROOT"
echo "PlatformIO: $PIO"
echo "Target: ESP32-S3 N16R8"

# 1. Git whitespace validation
run_check "Git diff check" git diff --check

# 2. Existing C++ host tests
run_check "HTTP telemetry host tests" \
    python3 tests/run_http_checks.py

# 3. Secure OTA tests
run_check "Secure OTA tests" \
    python3 -m unittest discover \
    -s tools -p 'test_secure_ota.py' -v

# 4. USB firmware build
run_check "S3 USB firmware build" \
    "$PIO" run -e esp32-s3-n16r8-tft

# 5. OTA firmware build
run_check "S3 OTA firmware build" \
    "$PIO" run -e esp32-s3-n16r8-tft-ota

# 6. Firmware artifacts
step "FIRMWARE ARTIFACTS"

for ENV in esp32-s3-n16r8-tft esp32-s3-n16r8-tft-ota; do
    BIN=".pio/build/$ENV/firmware.bin"

    if [[ -s "$BIN" ]]; then
        SIZE="$(stat -c '%s' "$BIN")"
        HUMAN="$(du -h "$BIN" | cut -f1)"
        echo "[PASS] $ENV"
        echo "       Size: $SIZE bytes ($HUMAN)"
    else
        echo "[FAIL] Missing firmware: $BIN"
        ((FAIL+=1))
    fi
done

# Final report
step "FINAL REPORT"

echo "Passed checks: $PASS"
echo "Failed checks: $FAIL"

echo
echo "Git status:"
git status --short

echo
if (( FAIL == 0 )); then
    echo "RESULT: ALL CHECKS PASSED"
else
    echo "RESULT: VALIDATION FAILED"
fi

echo
echo "SAFETY:"
echo "- No firmware uploaded"
echo "- No USB flash"
echo "- No OTA"
echo "- No device reset"
echo "- No Git commit/stash/merge"

(( FAIL == 0 ))
