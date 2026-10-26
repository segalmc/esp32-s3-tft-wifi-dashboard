#include "LocalWssReceivePolicy.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  using local_wss_receive_policy::ReadAction;
  using local_wss_receive_policy::read_action;
  using local_wss_receive_policy::remaining_wait;
  using local_wss_receive_policy::SessionRef;
  using local_wss_receive_policy::remember_session;
  using local_wss_receive_policy::remove_session;
  using local_wss_receive_policy::session_for_fd;
  using local_wss_receive_policy::FrameReceiveStats;
  assert(local_wss_receive_policy::kReadBudgetMs == 2000);
  assert(local_wss_receive_policy::kSocketReadSliceMs == 100);
  constexpr int want_read = -0x6900;
  constexpr int want_write = -0x6880;
  constexpr int timeout = -0x6800;

  assert(read_action(want_read, want_read, want_write) == ReadAction::retry);
  assert(read_action(want_write, want_read, want_write) == ReadAction::retry);
  assert(local_wss_receive_policy::retryable_tls_io(-want_read, want_read, want_write));
  assert(local_wss_receive_policy::retryable_tls_io(-want_write, want_read, want_write));
  assert(read_action(0, want_read, want_write) == ReadAction::eof);
  assert(read_action(47, want_read, want_write) == ReadAction::data);
  assert(read_action(-0x0050, want_read, want_write) == ReadAction::fatal);
  assert(read_action(timeout, want_read, want_write) == ReadAction::fatal);

  assert(remaining_wait(100, 199, 2000) == 1901);
  assert(remaining_wait(100, 2100, 2000) == 0);
  assert(remaining_wait(UINT32_MAX - 500, 499, 1000) == 0);
  assert(remaining_wait(UINT32_MAX - 500, 100, 1000) == 399);
  assert(read_action(-0x0050, want_read, want_write) == ReadAction::fatal); // reset
  assert(read_action(-0x7280, want_read, want_write) == ReadAction::fatal); // TLS alert

  FrameReceiveStats frame_stats{};
  local_wss_receive_policy::observe_read(frame_stats, 128, want_read, want_read, want_write, 97);
  assert(frame_stats.calls == 1 && frame_stats.would_block == 1 && frame_stats.bytes == 0);
  assert(local_wss_receive_policy::timeout_evidence(frame_stats) ==
         local_wss_receive_policy::TimeoutEvidence::no_bytes);
  local_wss_receive_policy::observe_poll(frame_stats, 0, 2000);
  local_wss_receive_policy::observe_read(frame_stats, 128, 17, want_read, want_write, 4);
  assert(frame_stats.calls == 2 && frame_stats.positive_reads == 1 &&
         frame_stats.partial_reads == 1 && frame_stats.bytes == 17 &&
         frame_stats.poll_timeout == 1 && frame_stats.wait_ms == 2000 &&
         frame_stats.read_ms == 101 && frame_stats.read_max_ms == 97);
  assert(local_wss_receive_policy::timeout_evidence(frame_stats) ==
         local_wss_receive_policy::TimeoutEvidence::partial_bytes);

  int tls_a = 0, tls_b = 0, tls_c = 0;
  SessionRef sessions[2]{{-1, nullptr}, {-1, nullptr}};
  assert(remember_session(sessions, 2, 54, &tls_a));
  assert(remember_session(sessions, 2, 55, &tls_b));
  assert(session_for_fd(sessions, 2, 54) == &tls_a);
  assert(!remember_session(sessions, 2, 56, &tls_c)); // bounded capacity
  assert(remove_session(sessions, 2, &tls_a));
  assert(session_for_fd(sessions, 2, 54) == nullptr); // cleanup before fd reuse
  assert(remember_session(sessions, 2, 54, &tls_c));
  assert(session_for_fd(sessions, 2, 54) == &tls_c);
  assert(!remove_session(sessions, 2, &tls_a));
  assert(remove_session(sessions, 2, &tls_b));
  assert(remove_session(sessions, 2, &tls_c));
  assert(session_for_fd(sessions, 2, 54) == nullptr);
  std::cout << "S3 TLS receive retry and session cleanup tests passed\n";
}
