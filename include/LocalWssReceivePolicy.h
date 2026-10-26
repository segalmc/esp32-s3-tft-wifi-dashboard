#pragma once
#include <stdint.h>
#include <stddef.h>

namespace local_wss_receive_policy {
constexpr uint32_t kReadBudgetMs = 2000;
constexpr uint32_t kSocketReadSliceMs = 100;
enum class ReadAction { data, eof, retry, fatal };

struct SessionRef { int fd; void *tls; };
inline bool retryable_tls_io(int error, int want_read, int want_write);

struct FrameReceiveStats {
  uint32_t calls = 0;
  uint32_t positive_reads = 0;
  uint32_t bytes = 0;
  uint32_t partial_reads = 0;
  uint32_t would_block = 0;
  uint32_t poll_ready = 0;
  uint32_t poll_timeout = 0;
  uint32_t poll_errors = 0;
  uint32_t wait_ms = 0;
  uint32_t read_ms = 0;
  uint32_t read_max_ms = 0;
  uint32_t last_requested = 0;
  int32_t last_result = 0;
};

inline void observe_read(FrameReceiveStats &stats, size_t requested, int result,
                         int want_read, int want_write, uint32_t elapsed_ms = 0) {
  ++stats.calls;
  stats.read_ms += elapsed_ms;
  if (elapsed_ms > stats.read_max_ms) stats.read_max_ms = elapsed_ms;
  stats.last_requested = requested > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(requested);
  stats.last_result = result;
  if (result > 0) {
    ++stats.positive_reads;
    stats.bytes += static_cast<uint32_t>(result);
    if (static_cast<size_t>(result) < requested) ++stats.partial_reads;
  } else if (retryable_tls_io(result, want_read, want_write)) {
    ++stats.would_block;
  }
}

inline void observe_poll(FrameReceiveStats &stats, int result, uint32_t waited_ms) {
  stats.wait_ms += waited_ms;
  if (result > 0) ++stats.poll_ready;
  else if (result == 0) ++stats.poll_timeout;
  else ++stats.poll_errors;
}

enum class TimeoutEvidence { no_bytes, partial_bytes };
inline TimeoutEvidence timeout_evidence(const FrameReceiveStats &stats) {
  return stats.bytes ? TimeoutEvidence::partial_bytes : TimeoutEvidence::no_bytes;
}

inline bool remember_session(SessionRef *sessions, size_t count, int fd, void *tls) {
  if (!sessions || fd < 0 || !tls) return false;
  for (size_t i = 0; i < count; ++i) {
    if (sessions[i].tls == tls || sessions[i].fd < 0) {
      sessions[i] = {fd, tls};
      return true;
    }
  }
  return false;
}

inline bool remove_session(SessionRef *sessions, size_t count, void *tls) {
  if (!sessions || !tls) return false;
  for (size_t i = 0; i < count; ++i) {
    if (sessions[i].tls == tls) {
      sessions[i] = {-1, nullptr};
      return true;
    }
  }
  return false;
}

inline void *session_for_fd(const SessionRef *sessions, size_t count, int fd) {
  if (!sessions || fd < 0) return nullptr;
  for (size_t i = 0; i < count; ++i)
    if (sessions[i].fd == fd) return sessions[i].tls;
  return nullptr;
}

inline bool retryable_tls_io(int error, int want_read, int want_write) {
  return error == want_read || error == -want_read ||
         error == want_write || error == -want_write;
}

inline ReadAction read_action(int result, int want_read, int want_write) {
  if (result > 0) return ReadAction::data;
  if (result == 0) return ReadAction::eof;
  return retryable_tls_io(result, want_read, want_write)
             ? ReadAction::retry : ReadAction::fatal;
}

inline uint32_t remaining_wait(uint32_t started_ms, uint32_t now_ms,
                               uint32_t budget_ms) {
  const uint32_t elapsed = now_ms - started_ms;
  return elapsed >= budget_ms ? 0 : budget_ms - elapsed;
}
}  // namespace local_wss_receive_policy
