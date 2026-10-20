#!/usr/bin/env python3
"""Privately save the 20-character password shown by `ota show` on the TFT."""

import getpass
import os
from pathlib import Path
import stat
import sys
import tempfile

from secure_espota import ALPHABET, PASSWORD_PATH


def store_password(password, path=PASSWORD_PATH):
    if len(password) != 20 or any(char not in ALPHABET for char in password):
        raise ValueError("expected the 20-character password shown on the TFT")
    directory = path.parent
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    parent = directory.lstat()
    if (not stat.S_ISDIR(parent.st_mode) or parent.st_uid != os.getuid()
            or stat.S_IMODE(parent.st_mode) != 0o700):
        raise ValueError("OTA secret directory must be owned by this user and mode 0700")
    try:
        existing = path.lstat()
    except FileNotFoundError:
        pass
    else:
        if (not stat.S_ISREG(existing.st_mode) or existing.st_uid != os.getuid()
                or stat.S_IMODE(existing.st_mode) != 0o600 or existing.st_nlink != 1):
            raise ValueError("existing OTA secret file is unsafe; correct it before replacing")

    descriptor, temporary = tempfile.mkstemp(prefix=".ota-password-", dir=directory)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as output:
            output.write(password.encode("ascii"))
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, path)
        directory_fd = os.open(directory, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    password = getpass.getpass("OTA password shown on TFT: ")
    confirmation = getpass.getpass("Enter it again: ")
    if password != confirmation:
        print("Passwords did not match; secret file unchanged", file=sys.stderr)
        return 1
    try:
        store_password(password)
    except (OSError, ValueError) as exc:
        print(f"Could not save OTA password: {exc}", file=sys.stderr)
        return 1
    print("OTA password saved privately (mode 0600)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
