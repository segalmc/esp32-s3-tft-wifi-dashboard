#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

ENV_NAME="esp32-s3-n16r8-tft-ota"
PASSWORD_DIR="$HOME/.config/nature-glass"
PASSWORD_FILE="$PASSWORD_DIR/ota-password"
FIRMWARE=".pio/build/$ENV_NAME/firmware.bin"

echo "=========================================="
echo " ESP32-S3 SECURE OTA DEPLOYMENT"
echo "=========================================="

# Locate PlatformIO
if command -v pio >/dev/null 2>&1; then
    PIO="$(command -v pio)"
elif [[ -x "$HOME/.platformio/penv/bin/pio" ]]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "ERROR: PlatformIO not found."
    exit 1
fi

# Verify OTA configuration
if ! grep -q 'secure_ota_upload.py' platformio.ini; then
    echo "ERROR: Secure OTA upload hook not configured."
    exit 1
fi

if ! grep -Eq 'upload_protocol[[:space:]]*=[[:space:]]*espota' platformio.ini; then
    echo "ERROR: OTA protocol is not configured as espota."
    exit 1
fi

# Verify credentials without displaying them
if [[ ! -f "$PASSWORD_FILE" || -L "$PASSWORD_FILE" ]]; then
    echo "ERROR: OTA password file missing or invalid."
    exit 1
fi

if [[ "$(stat -c '%a' "$PASSWORD_FILE")" != "600" ]]; then
    echo "ERROR: OTA password permissions must be 600."
    exit 1
fi

if [[ "$(stat -c '%a' "$PASSWORD_DIR")" != "700" ]]; then
    echo "ERROR: OTA directory permissions must be 700."
    exit 1
fi

# Get target IP
echo
read -r -p "Enter ESP32-S3 IP address: " DEVICE_IP

if ! python3 -c \
    'import ipaddress,sys; ipaddress.IPv4Address(sys.argv[1])' \
    "$DEVICE_IP" 2>/dev/null; then
    echo "ERROR: Invalid IPv4 address."
    exit 1
fi

# Build
echo
echo "Building $ENV_NAME ..."

"$PIO" run -e "$ENV_NAME"

[[ -s "$FIRMWARE" ]] || {
    echo "ERROR: Firmware artifact missing."
    exit 1
}

echo
echo "Firmware information:"
stat -c 'Size: %s bytes' "$FIRMWARE"
sha256sum "$FIRMWARE"

# Explicit approval
echo
echo "=========================================="
echo " DEPLOYMENT TARGET"
echo "=========================================="
echo "Device: ESP32-S3 N16R8"
echo "IP:     $DEVICE_IP"
echo "Image:  $FIRMWARE"
echo
echo "WARNING: This will upload firmware to the S3."
echo "The original ESP32 will NOT be touched."
echo

read -r -p "Type UPLOAD $DEVICE_IP to continue: " CONFIRM

if [[ "$CONFIRM" != "UPLOAD $DEVICE_IP" ]]; then
    echo "Deployment cancelled. No upload performed."
    exit 0
fi

# Secure OTA upload
echo
echo "Starting authenticated OTA upload..."

NATURE_GLASS_OTA_IP="$DEVICE_IP" \
    "$PIO" run -e "$ENV_NAME" -t upload

echo
echo "=========================================="
echo " OTA UPLOAD COMMAND COMPLETED"
echo "=========================================="
echo "Target: $DEVICE_IP"
echo "Firmware SHA256:"
sha256sum "$FIRMWARE"

echo
echo "IMPORTANT:"
echo "- Upload command completed."
echo "- Device firmware version still needs verification."
echo "- Telemetry liveness must be tested after reboot."
echo "- Original ESP32 was not modified."
