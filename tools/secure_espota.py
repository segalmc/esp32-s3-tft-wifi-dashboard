#!/usr/bin/env python3
"""Invoke PlatformIO's bundled ESPOTA without exposing its password in argv."""

import importlib.util
import logging
import os
from pathlib import Path
import random
import stat
import sys


ALPHABET = frozenset("ABCDEFGHJKLMNPQRSTUVWXYZ23456789")
PASSWORD_PATH = Path.home() / ".config" / "nature-glass" / "ota-password"


def load_password(path=PASSWORD_PATH):
    directory = path.parent
    parent = directory.lstat()
    if not stat.S_ISDIR(parent.st_mode) or parent.st_uid != os.getuid():
        raise ValueError("OTA secret directory must be owned by this user")
    if stat.S_IMODE(parent.st_mode) & 0o077:
        raise ValueError("OTA secret directory must be private (0700)")

    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_CLOEXEC", 0)
    descriptor = os.open(path, flags)
    try:
        info = os.fstat(descriptor)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or stat.S_IMODE(info.st_mode) != 0o600 or info.st_nlink != 1):
            raise ValueError("OTA secret file must be owned by this user, regular, and mode 0600")
        raw = os.read(descriptor, 64)
    finally:
        os.close(descriptor)
    try:
        password = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("OTA secret file has an invalid format") from exc
    if len(password) != 20 or any(char not in ALPHABET for char in password):
        raise ValueError("OTA secret file has an invalid format")
    return password


def upload(espota_path, host, firmware_path, password):
    spec = importlib.util.spec_from_file_location("natureglass_espota", espota_path)
    if spec is None or spec.loader is None:
        raise RuntimeError("Bundled ESPOTA uploader is unavailable")
    espota = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(espota)
    espota.PROGRESS = True
    espota.TIMEOUT = 10
    local_port = random.SystemRandom().randint(10000, 60000)
    # This calls the same transport used by PlatformIO's normal espota command.
    # The secret exists only in this process and the authenticated exchange.
    result = espota.serve(
        remote_addr=host,
        local_addr="0.0.0.0",
        remote_port=3232,
        local_port=local_port,
        password=password,
        md5_target=None,
        filename=firmware_path,
        command=espota.FLASH,
    )
    if type(result) is not int or result not in (0, 1):
        logging.error("ESPOTA returned an invalid result")
        return 1
    return result


def main(argv):
    if len(argv) != 4:
        print("OTA upload configuration error", file=sys.stderr)
        return 2
    try:
        password = load_password()
    except (OSError, ValueError) as exc:
        print(f"OTA password file error: {exc}", file=sys.stderr)
        return 2
    logging.basicConfig(level=logging.WARNING)
    return upload(argv[1], argv[2], argv[3], password)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
