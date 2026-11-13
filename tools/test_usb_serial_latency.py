#!/usr/bin/env python3
"""Exercise the installed USB driver's actual write() under host backpressure.

No device is opened. FreeRTOS waits are counted by host fakes. The timeout is
read from firmware setup, so this also checks the deployed call-site policy.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
setup = (root / "src/main.cpp").read_text().split("void setup()", 1)[1]
before_logs = setup.split('Serial.println(', 1)[0]
match = re.search(r"Serial\.setTxTimeoutMs\((\d+)\)", before_logs)
assert match, "USB timeout must be configured before startup logging"
timeout = int(match[1])
driver = (Path.home() / ".platformio/packages/framework-arduinoespressif32/cores/esp32/HWCDC.cpp").read_text()
start = driver.index("size_t HWCDC::write(const uint8_t *buffer, size_t size)")
opening = driver.index("{", start)
depth = 1
end = opening + 1
while depth:
    depth += (driver[end] == "{") - (driver[end] == "}")
    end += 1
method = driver[start:end]
fake = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
constexpr int pdPASS=1, pdTRUE=1, portTICK_PERIOD_MS=1;
int ring, lock;
int* tx_ring_buf=&ring;
int* tx_lock=&lock;
uint32_t tx_timeout_ms=100, waited=0;
bool connected=true, stalled=false, locked=false;
int xSemaphoreTake(int*, uint32_t ticks) {
    if (locked) { waited+=ticks; return 0; }
    return pdPASS;
}
void xSemaphoreGive(int*) {}
size_t xRingbufferGetMaxItemSize(int*) { return 64; }
int xRingbufferSend(int*, void*, size_t, uint32_t ticks) {
    if (stalled) { waited+=ticks; return 0; }
    return pdTRUE;
}
bool isCDC_Connected() { return connected; }
bool isPlugged() { return true; }
void flushTXBuffer(const uint8_t*, size_t) {}
void hw_cdc_enable_tx_intr() {}
class HWCDC { public: size_t write(const uint8_t*, size_t); };
'''
checks = r'''
int main() {
    HWCDC serial;
    const uint8_t message[256]={};
    assert(serial.write(message,sizeof(message))==sizeof(message));
    stalled=true;
    serial.write(message,sizeof(message));
    assert(waited>=720); // Old default exceeds the sender's ACK deadline.
    tx_timeout_ms=CONFIGURED_TIMEOUT;
    waited=0;
    assert(serial.write(message,sizeof(message))==0);
    assert(waited==0); // Stalled USB host cannot delay radio responses.
    locked=true;
    assert(serial.write(message,sizeof(message))==0);
    assert(waited==0); // Nor can another task holding the diagnostic lock.
    locked=false; stalled=false;
    assert(serial.write(message,sizeof(message))==sizeof(message));
}
'''.replace("CONFIGURED_TIMEOUT", str(timeout))
with tempfile.TemporaryDirectory(prefix="heating-usb-test-") as tmp:
    source = Path(tmp) / "test.cpp"
    source.write_text(fake + method + checks)
    binary = Path(tmp) / "test"
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS actual USB write: host stall, diagnostic lock, resume; zero blocking wait")
