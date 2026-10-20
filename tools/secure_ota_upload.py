"""PlatformIO post script: replace ESPOTA's argument-based auth uploader.

The password is read by secure_espota.py after process launch. Never put it in
UPLOAD_FLAGS, the command line, the SCons environment, or a verbose build log.
"""

import ipaddress
import os

Import("env")

if env.subst("$UPLOAD_PROTOCOL") == "espota":
    override = os.environ.get("NATURE_GLASS_OTA_IP")
    if override:
        try:
            ipaddress.IPv4Address(override)
        except ipaddress.AddressValueError as exc:
            raise ValueError("NATURE_GLASS_OTA_IP must be an IPv4 address") from exc
        env.Replace(UPLOAD_PORT=override)
    # Drop inherited uploader flags, including any legacy --auth value.
    env.Replace(
        UPLOADERFLAGS=[],
        UPLOADCMD='"$PYTHONEXE" "$PROJECT_DIR/tools/secure_espota.py" '
                  '"$UPLOADER" "$UPLOAD_PORT" "$SOURCE"'
    )
