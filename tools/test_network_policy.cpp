#include "NetworkPolicy.h"

#include <assert.h>
#include <stdio.h>

int main() {
  using namespace network_policy;
  uint32_t delay = 1000;
  const uint32_t expected[] = {2000, 4000, 8000, 16000, 32000, 60000, 60000};
  for (uint32_t next : expected) {
    delay = nextRetryDelay(delay);
    assert(delay == next);
  }
  assert(!recoveryDue(true, false, 299999, 0));
  assert(recoveryDue(true, false, 300000, 0));
  assert(!recoveryDue(true, true, 600000, 0));  // LAN works even if NTP does not.
  assert(!recoveryDue(false, false, 600000, 0));
  assert(due(10, UINT32_MAX - 5));  // Millisecond rollover.
  assert(validHomeCredentials("My network", 33, "correct-horse", 65));
  assert(!validHomeCredentials("", 33, "correct-horse", 65));
  assert(!validHomeCredentials("My network", 33, "short", 65));
  assert(!validHomeCredentials("bad\nname", 33, "correct-horse", 65));
  assert(!validHomeCredentials("My network", 33, "bad\nsecret", 65));
  struct Candidate { int id; } saved{1}, candidate{2};
  assert(!commitAfterPersistence(saved, candidate, [](const Candidate&) { return false; }));
  assert(saved.id == 1);  // Failed NVS write must retain the old network.
  assert(commitAfterPersistence(saved, candidate, [](const Candidate&) { return true; }));
  assert(saved.id == 2);
  puts("Network policy tests passed");
}
