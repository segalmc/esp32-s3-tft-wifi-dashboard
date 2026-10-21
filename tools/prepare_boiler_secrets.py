#!/usr/bin/env python3
"""Prepare ignored firmware secrets from the local, read-only Node proxy files."""
import hashlib
import hmac
import json
import ipaddress
import os
from pathlib import Path
import re
import secrets

ROOT = Path(__file__).resolve().parents[1]
PROXY = Path('/home/michael/hotWaterCtrl/localProxyTuya')


def require(pattern, source, name):
    match = re.search(pattern, source)
    if not match:
        raise SystemExit(f'Missing {name}; no firmware configuration written')
    return match.group(1)


def load_env(path):
    if not path.is_file():
        raise SystemExit('Active proxy environment file absent; Device ID cannot be verified')
    values = {}
    for line in path.read_text().splitlines():
        if '=' in line and not line.lstrip().startswith('#'):
            key, value = line.split('=', 1)
            values[key.strip()] = value.strip().strip('"\'')
    return values


def c_string(value):
    if not value.isascii() or any(ord(ch) < 32 or ord(ch) > 126 for ch in value):
        raise SystemExit('Credential contains unexpected characters')
    return json.dumps(value)


bridge = (PROXY / 'bridge.js').read_text()
server = (PROXY / 'server.js').read_text()
bridge_key = require(r'key\s*:\s*[\'\"]([^\'\"]+)[\'\"]', bridge, 'bridge Local Key')
device_ip = require(r'ip\s*:\s*[\'\"](\d+\.\d+\.\d+\.\d+)[\'\"]', bridge, 'bridge IP')
if not re.search(r'key\s*:\s*process\.env\.LOCALKEY', server) or not re.search(
        r'id\s*:\s*process\.env\.DEVICEID', server) or not re.search(
        r'version\s*:\s*[\'\"]3\.4[\'\"]', server):
    raise SystemExit('Active proxy key, ID, or protocol configuration does not match expectations')
env = load_env(PROXY / '.env')
key = env.get('LOCALKEY', '')
device_id = env.get('DEVICEID', '')
if len(key) != 16 or not hmac.compare_digest(key, bridge_key) or not device_id:
    raise SystemExit('Proxy credentials are missing or the keys disagree')
try:
    if not ipaddress.ip_address(device_ip).is_private:
        raise ValueError('not a private address')
except ValueError:
    raise SystemExit('Device IP must be a private LAN address')

password_path = ROOT / '.boiler-web-password'
if password_path.exists():
    password = password_path.read_text().strip()
    if len(password) < 24:
        raise SystemExit('Existing Web password is too short')
else:
    password = secrets.token_urlsafe(24)
    fd = os.open(password_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        stream.write(password + '\n')
os.chmod(password_path, 0o600)
salt = secrets.token_bytes(16)
digest = hashlib.sha256(salt + password.encode()).digest()
output = ROOT / 'include' / 'secrets.h'
output.write_text('''#pragma once
// Generated locally. Never commit or print this file.
static constexpr char TUYA_LOCAL_KEY[] = %s;
static constexpr char TUYA_DEVICE_ID[] = %s;
static constexpr char TUYA_DEVICE_IP[] = %s;
static constexpr unsigned char WEB_PASSWORD_SALT[16] = {%s};
static constexpr unsigned char WEB_PASSWORD_HASH[32] = {%s};
''' % (c_string(key), c_string(device_id), c_string(device_ip),
       ','.join(str(x) for x in salt), ','.join(str(x) for x in digest)))
os.chmod(output, 0o600)
print('Ignored local firmware configuration prepared; credentials withheld')
