#!/usr/bin/env python3
"""Compile actual TuyaControl.cpp with mocked hardware, storage, time and tasks.

Only dependency includes in a temporary copy of TuyaControl.h are redirected;
production controller source and policy code are compiled without replacement.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="heating-control-") as temporary:
    temp = Path(temporary)
    header = (ROOT / "include/TuyaControl.h").read_text()
    for name in ["TftConfig.h", "Arduino.h", "Preferences.h", "NetworkManager.h", "TuyaLocalClient.h"]:
        header = header.replace('#include "' + name + '"', '#include "Runtime.h"')
        header = header.replace('#include <' + name + '>', '#include "Runtime.h"')
    (temp / "TuyaControl.h").write_text(header)
    (temp / "secrets.h").write_text('#define TUYA_DEVICE_IP "192.0.2.1"\n#define TUYA_DEVICE_ID "mock-device"\n#define TUYA_LOCAL_KEY "mock-only-key0000"\n')
    arduino = next((ROOT / ".pio/libdeps").glob("*/ArduinoJson/src"))
    command = ["g++", "-std=c++17", "-DTFT_HARDWARE_ENABLED=1", "-I" + str(temp),
               "-I" + str(ROOT / "tools/heating_control_stubs"), "-I" + str(ROOT / "include"),
               "-I" + str(arduino), str(ROOT / "tools/heating_control_stubs/control_test.cpp"),
               "-o", str(temp / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(temp / "test")], check=True)
