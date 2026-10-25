#include "LocalWssPolicy.h"
#include <assert.h>
#include <stdio.h>

int main() {
  using local_wss_policy::stale;
  assert(!stale(true, true, "reported", 30000, 0));
  assert(stale(true, true, "reported", 30001, 0));
  assert(stale(false, true, "reported", 1, 0));
  assert(stale(true, false, "reported", 1, 0));
  assert(stale(true, true, "stale", 1, 0));
  assert(stale(true, true, "unknown", 1, 0));
  assert(!stale(true, true, "reported", 10, UINT32_MAX - 9));
  puts("Local WSS freshness policy tests passed");
}
