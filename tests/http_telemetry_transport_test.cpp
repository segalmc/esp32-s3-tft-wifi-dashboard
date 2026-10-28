#include "../include/HttpTelemetryTransport.h"
#include <cassert>
using namespace http_telemetry;
int main() {
    ChallengeGuard guard;
    guard.issue(42, 1000);
    assert(!guard.consume(41, 1001));
    assert(!guard.consume(42, 1002)); // failed attempt consumed it
    guard.issue(43, 2000);
    assert(guard.consume(43, 2000 + kChallengeLifetimeMs));
    assert(!guard.consume(43, 2001)); // replay
    guard.issue(44, 3000);
    assert(!guard.consume(44, 3000 + kChallengeLifetimeMs + 1));
    assert(next_backoff(1000, 60000) == 2000);
    assert(next_backoff(40000, 60000) == 60000);
    assert(next_backoff(60000, 60000) == 60000);
    static_assert(kHttpConnectTimeoutMs <= 2000);
    static_assert(kHttpReadTimeoutMs <= 2000);
}
