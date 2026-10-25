#pragma once
#include <stdint.h>
#include <string.h>

namespace local_wss_policy {
constexpr uint32_t kTelemetryMaxAgeMs = 30000;

inline bool stale(bool connected, bool valid, const char* quality,
                  uint32_t now_ms, uint32_t received_ms) {
  return !connected || !valid || !quality || strcmp(quality, "reported") != 0 ||
         static_cast<uint32_t>(now_ms - received_ms) > kTelemetryMaxAgeMs;
}
}  // namespace local_wss_policy
