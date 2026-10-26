#pragma once
#include <stdint.h>

namespace local_wss_clock_policy {
inline bool jumped(int64_t wall_seconds, uint32_t elapsed_ms) {
  const int64_t elapsed = elapsed_ms / 1000;
  const int64_t drift = wall_seconds - elapsed;
  return drift > 30 || drift < -30;
}
inline bool recovered(bool synced, uint32_t observed_generation,
                      uint32_t required_after_generation, int64_t utc_seconds) {
  return synced && observed_generation != required_after_generation &&
         utc_seconds >= 1700000000;
}
inline bool retry_due(uint32_t now_ms, uint32_t last_request_ms) {
  return static_cast<uint32_t>(now_ms - last_request_ms) >= 60000;
}
inline bool stop_listener(bool running, bool ready, bool same_ip) {
  return running && (!ready || !same_ip);
}
inline bool start_listener(bool running, bool ready) {
  return !running && ready;
}
}  // namespace local_wss_clock_policy
