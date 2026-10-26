#include "LocalWssClockPolicy.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace local_wss_clock_policy;
  assert(!jumped(30, 0));
  assert(jumped(31, 0));
  assert(jumped(-31, 0));
  assert(!jumped(61, 31000));
  // A prior sync or a disconnected link cannot clear a clock-jump latch.
  for (unsigned retry = 0; retry < 8; ++retry) {
    assert(!recovered(true, 7, 7, 1800000000));
    assert(!recovered(false, 8, 7, 1800000000));
    assert(!recovered(true, 8, 7, 100));
  }
  assert(recovered(true, 8, 7, 1800000000));
  assert(!retry_due(59999, 0));
  assert(retry_due(60000, 0));
  assert(retry_due(100, 0xffff0000U));
  // Existing socket is closed on an S3 IP change and opens at the new IP.
  assert(stop_listener(true, true, false));
  assert(start_listener(false, true));
  assert(!stop_listener(true, true, true));
  // Reboot has no running socket, then starts after trusted time arrives.
  assert(!start_listener(false, false));
  assert(start_listener(false, true));
  assert(stop_listener(true, false, true));
  assert(start_listener(false, true)); // repeated failures can recover
  std::cout << "S3 WSS clock recovery tests passed\n";
}
