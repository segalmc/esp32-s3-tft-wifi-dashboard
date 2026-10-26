#pragma once

#include <cstdint>

namespace local_wss_tls_failure_policy {
enum class Kind : uint8_t {
  unreported,
  handshake_timeout,
  transport_closed,
  certificate_verify,
  allocation,
  handshake_failure,
  other,
};

// These values come from the installed ESP-IDF 5.5.5 esp_tls_errors.h.
// The HTTPS event omits the 0x8009 code on the server timeout path, so an
// all-zero event must remain unreported rather than inferred as a timeout.
inline Kind classify(int esp_error, int tls_code, uint32_t cert_flags) {
  if (esp_error == 0x8009) return Kind::handshake_timeout;
  if (esp_error == 0x101) return Kind::allocation;
  if (cert_flags != 0) return Kind::certificate_verify;
  if (esp_error == 0x8008 || tls_code == -0x0050) return Kind::transport_closed;
  if (esp_error == 0x801A) return Kind::handshake_failure;
  if (esp_error == 0 && tls_code == 0) return Kind::unreported;
  return Kind::other;
}

inline const char* name(Kind kind) {
  switch (kind) {
    case Kind::unreported: return "unreported";
    case Kind::handshake_timeout: return "handshake-timeout";
    case Kind::transport_closed: return "transport-closed";
    case Kind::certificate_verify: return "certificate-verify";
    case Kind::allocation: return "allocation";
    case Kind::handshake_failure: return "handshake-failure";
    default: return "other";
  }
}
}  // namespace local_wss_tls_failure_policy
