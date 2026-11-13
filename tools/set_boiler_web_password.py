#!/usr/bin/env python3
"""Interactively prepare the ignored Web password and generated firmware hash.

Run in a local terminal. No password is accepted as a command argument or printed.
This prepares build inputs only; it never uploads firmware.
"""

import getpass
import hashlib
import hmac
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PASSWORD_FILE = ROOT / '.boiler-web-password'
HEADER_FILE = ROOT / 'include' / 'secrets.h'


def encoded_password(value):
    try:
        data = value.encode('ascii')
    except UnicodeEncodeError:
        raise ValueError('Use printable ASCII characters')
    if not 24 <= len(data) <= 128 or value != value.strip() or not value.isprintable():
        raise ValueError('Use 24–128 printable ASCII characters without surrounding whitespace')
    return data


def secret_array(source, name):
    match = re.search(r'\b' + name + r'\s*\[\d+\]\s*=\s*\{([^}]*)\}', source)
    if not match:
        raise ValueError('Generated firmware hash is missing')
    return bytes(int(part.strip()) for part in match.group(1).split(','))


def main():
    if not sys.stdin.isatty():
        raise SystemExit('Run this script in a local interactive terminal')
    first = getpass.getpass('New ESP32-S3 dashboard password (hidden): ')
    second = getpass.getpass('Repeat dashboard password (hidden): ')
    try:
        secret = encoded_password(first)
        confirmation = encoded_password(second)
    except ValueError as exc:
        raise SystemExit(str(exc))
    if not hmac.compare_digest(secret, confirmation):
        raise SystemExit('Password entries differ; nothing changed')
    if input('Type PREPARE to replace the ignored local password file: ') != 'PREPARE':
        raise SystemExit('Cancelled; nothing changed')

    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(PASSWORD_FILE, flags, 0o600)
    with os.fdopen(fd, 'wb') as stream:
        os.fchmod(stream.fileno(), 0o600)
        stream.write(secret + b'\n')
        stream.flush()
        os.fsync(stream.fileno())

    result = subprocess.run(
        [sys.executable, str(ROOT / 'tools' / 'prepare_boiler_secrets.py')],
        cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        check=False)
    if result.returncode:
        raise SystemExit('Firmware secret preparation failed; do not build or upload')
    source = HEADER_FILE.read_text()
    try:
        salt = secret_array(source, 'WEB_PASSWORD_SALT')
        digest = secret_array(source, 'WEB_PASSWORD_HASH')
    except ValueError as exc:
        raise SystemExit(str(exc))
    if not hmac.compare_digest(hashlib.sha256(salt + secret).digest(), digest):
        raise SystemExit('Generated firmware hash verification failed; do not build or upload')
    print('Local dashboard password and firmware hash prepared and verified; no firmware uploaded')


if __name__ == '__main__':
    main()
