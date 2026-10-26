#include "LocalWssTlsFailurePolicy.h"
#include <cassert>
#include <cstring>

int main() {
  using namespace local_wss_tls_failure_policy;
  assert(classify(0, 0, 0) == Kind::unreported);
  assert(classify(0x8009, 0, 0) == Kind::handshake_timeout);
  assert(classify(0x101, 0, 0) == Kind::allocation);
  assert(classify(0x801A, 0, 1) == Kind::certificate_verify);
  assert(classify(0x801A, 0, 0) == Kind::handshake_failure);
  assert(classify(0x8008, 0, 0) == Kind::transport_closed);
  assert(classify(0, -0x0050, 0) == Kind::transport_closed);
  assert(classify(0x8017, 0, 0) == Kind::other);
  assert(std::strcmp(name(classify(0, 0, 0)), "unreported") == 0);
}
