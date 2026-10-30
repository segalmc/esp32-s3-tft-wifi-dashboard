#include "../include/HttpTelemetryFreshness.h"
#include <cassert>
using namespace http_telemetry;
int main() {
    assert(communication(false, 0) == Communication::unknown);
    assert(measurement(false, 0) == Measurement::unknown);
    assert(communication(true, 30000) == Communication::live);
    assert(communication(true, 35000) == Communication::live);
    assert(communication(true, 35001) == Communication::delayed);
    assert(communication(true, 75001) == Communication::offline);
    assert(measurement(true, 75001) == Measurement::stale);
    // A heartbeat refreshes communication even when an old measurement is
    // still stale; it never changes the measurement clock by itself.
    const uint32_t now = 100000;
    const uint32_t last_message = 99000;
    const uint32_t last_measurement = 1000;
    assert(communication(true, now - last_message) == Communication::live);
    assert(measurement(true, now - last_measurement) == Measurement::stale);
}
