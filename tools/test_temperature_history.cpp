#include "TemperatureHistory.h"
#include <cassert>
#include <cstdio>
int main() {
  TemperatureHistory h;
  const int64_t now = 1800000000;
  assert(h.add(now - 40, now, 40, 51, true));
  assert(h.add(now - 35, now, 65, 49, true));
  auto b = h.at((now - 40) / 60);
  assert(b.boiler_min == 40 && b.boiler_max == 65);
  assert(b.solar_min == 49 && b.solar_max == 51);
  assert(!h.add(now, now, NAN, 30, true));
  assert(!h.add(now, now, 30, 30, false));
  assert(!h.add(now - 86401, now, 30, 30, true));
  assert(!h.add(now + 31, now, 30, 30, true));
  assert(h.at((now - 120) / 60).minute == -1); // outage gap
  h.expire(now + 86401);
  assert(h.at((now - 40) / 60).minute == -1);
  assert(h.add(now + 86401, now + 86401, 30, 31, true));
  h.expire(now - 100); // clock rollback clears misleading history
  assert(h.at((now + 86401) / 60).minute == -1);
  std::puts("temperature history tests passed");
}
