#include "OtaPolicy.h"

#include <assert.h>
#include <stdio.h>

int main() {
  using ota_policy::mayServe;
  assert(mayServe(true, false, true, true));
  assert(!mayServe(false, false, true, true));
  assert(!mayServe(true, true, true, true));
  assert(!mayServe(true, false, false, true));
  assert(!mayServe(true, false, true, false));
  puts("OTA eligibility tests passed");
}
