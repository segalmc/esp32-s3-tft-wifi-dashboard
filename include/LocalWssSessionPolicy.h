#pragma once

#include <cstdint>

namespace local_wss_session_policy {

// A live HTTPD session must not be mistaken for a closed client socket.
inline bool session_gone(bool list_ok, bool fd_present) {
  return list_ok && !fd_present;
}

struct UnclaimedWatch {
  int fd = -1;
  uint32_t first_seen_ms = 0;
  bool close_requested = false;
};

// ESP-IDF skips the URI handler for a successful WebSocket upgrade. Track
// sockets that have not sent a valid hello, including silent upgraded peers.
inline int expired_unclaimed(UnclaimedWatch *watches, unsigned capacity,
                             const int *fds, unsigned count, int claimed_fd,
                             uint32_t now_ms, uint32_t timeout_ms) {
  if (!watches || !fds) return -1;
  for (unsigned i = 0; i < capacity; ++i) {
    bool present = false;
    for (unsigned j = 0; j < count; ++j)
      present |= fds[j] == watches[i].fd;
    if (!present || watches[i].fd == claimed_fd)
      watches[i] = {};
  }
  for (unsigned j = 0; j < count; ++j) {
    const int fd = fds[j];
    if (fd < 0 || fd == claimed_fd) continue;
    unsigned slot = capacity;
    for (unsigned i = 0; i < capacity; ++i)
      if (watches[i].fd == fd) { slot = i; break; }
    if (slot == capacity) {
      for (unsigned i = 0; i < capacity; ++i)
        if (watches[i].fd < 0) { slot = i; break; }
      if (slot == capacity) continue;
      watches[slot] = {fd, now_ms, false};
    }
    if (!watches[slot].close_requested &&
        static_cast<uint32_t>(now_ms - watches[slot].first_seen_ms) >= timeout_ms) {
      watches[slot].close_requested = true;
      return fd;
    }
  }
  return -1;
}

inline bool auth_expired(int client_fd, bool ready, uint32_t entered_ms,
                         uint32_t now_ms, uint32_t timeout_ms) {
  return client_fd >= 0 && !ready &&
         static_cast<uint32_t>(now_ms - entered_ms) >= timeout_ms;
}

}  // namespace local_wss_session_policy
