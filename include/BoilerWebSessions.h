#pragma once
#include "BoilerWebPolicy.h"
#include <cstdint>
#include <cstring>

namespace boiler_web_sessions {
constexpr size_t kMaxSessions = 4;
struct Session {
  char token[65] = {};
  char csrf[33] = {};
  uint32_t created = 0;
  uint32_t seen = 0;
};

class Table {
 public:
  Session* find(const char* cookie, uint32_t now) {
    expire(now);
    for (auto& session : sessions_) {
      if (boiler_web_policy::cookieHasSession(cookie, session.token)) {
        session.seen = now;
        return &session;
      }
    }
    return nullptr;
  }
  Session* allocate(uint32_t now) {
    expire(now);
    Session* slot = &sessions_[0];
    for (auto& session : sessions_) {
      if (!session.token[0]) { slot = &session; break; }
      if (static_cast<uint32_t>(now - session.seen) >
          static_cast<uint32_t>(now - slot->seen)) slot = &session;
    }
    invalidate(slot);
    slot->created = slot->seen = now;
    return slot;
  }
  void invalidate(Session* session) {
    if (!session) return;
    std::memset(session->token, 0, sizeof(session->token));
    std::memset(session->csrf, 0, sizeof(session->csrf));
    session->created = session->seen = 0;
  }
  void clear() { for (auto& session : sessions_) invalidate(&session); }
 private:
  void expire(uint32_t now) {
    for (auto& session : sessions_)
      if (session.token[0] && !boiler_web_policy::sessionAlive(
          true, session.created, session.seen, now)) invalidate(&session);
  }
  Session sessions_[kMaxSessions] = {};
};
} // namespace boiler_web_sessions
