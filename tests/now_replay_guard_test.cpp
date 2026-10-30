#include "../include/NowReplayGuard.h"
#include <cassert>
using now_telemetry::ReplayGuard;
int main() {
    ReplayGuard guard;
    assert(!guard.allow(0, 1, 10));
    assert(!guard.allow(2, 1, 10));
    assert(guard.allow(1, 1, 10));
    guard.accepted(1, 10);
    assert(!guard.allow(1, 1, 10));
    assert(!guard.allow(1, 1, 9));
    assert(guard.allow(1, 1, 11));
    guard.accepted(1, 11);
    assert(guard.allow(2, 2, 1)); // fresh sender boot after discovery
    assert(!guard.allow(2, 1, 12));
}
