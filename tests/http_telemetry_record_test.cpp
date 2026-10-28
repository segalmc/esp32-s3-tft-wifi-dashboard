#include "../include/HttpTelemetryRecord.h"
#include <cassert>
#include <cstring>

using namespace http_telemetry;
int main() {
    Record a{};
    a.version = kVersion;
    a.flags = kBoilerValid | kSolarValid;
    a.device_id = 9;
    a.boot_nonce = 100;
    a.sequence = 1;
    a.boiler_centi = 4212;
    a.solar_centi = 5133;
    seal(a);
    assert(valid(a, 9));
    assert(!valid(a, 10));
    auto broken = a;
    broken.solar_centi ^= 1;
    assert(!valid(broken, 9));
    broken = a;
    broken.flags |= kClockValid;
    seal(broken);
    assert(!valid(broken, 9));
    broken = a;
    broken.epoch_s = 42;
    seal(broken);
    assert(!valid(broken, 9));
    broken = a;
    broken.valve = 7;
    seal(broken);
    assert(!valid(broken, 9));
    Record sent[] = {a, a};
    sent[1].sequence = 2;
    seal(sent[1]);
    assert(ack_prefix(sent, 2, sent, 1));
    assert(ack_prefix(sent, 2, sent, 2));
    assert(!ack_prefix(sent, 2, sent + 1, 1));
    assert(!ack_prefix(sent, 1, sent, 2));
    assert(freshness(false, 0, true) == Freshness::unknown);
    assert(freshness(true, 7000, true) == Freshness::live);
    assert(freshness(true, 7001, true) == Freshness::delayed);
    assert(freshness(true, 15001, true) == Freshness::stale);
    assert(freshness(true, 30001, true) == Freshness::offline);
    assert(freshness(true, 0, false) == Freshness::offline);
}
