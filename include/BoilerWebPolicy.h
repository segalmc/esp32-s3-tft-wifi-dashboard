#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace boiler_web_policy {
constexpr char kReferrerPolicy[] = "same-origin";
enum class Action : uint8_t { Invalid, Refresh, Reconnect, On, Off };
inline Action parseAction(const char* value) {
  if (!value) return Action::Invalid;
  if (strcmp(value, "refresh") == 0) return Action::Refresh;
  if (strcmp(value, "reconnect") == 0) return Action::Reconnect;
  if (strcmp(value, "on") == 0) return Action::On;
  if (strcmp(value, "off") == 0) return Action::Off;
  return Action::Invalid;
}
inline const char* actionName(Action action) {
  switch (action) {
    case Action::Refresh: return "refresh";
    case Action::Reconnect: return "reconnect";
    case Action::On: return "on";
    case Action::Off: return "off";
    default: return "invalid";
  }
}
struct Availability {
  bool online;
  bool readGate;
  bool controlGate;
  bool connected;
  bool confirmedOff;
  bool fresh;
  bool leaseActive;
  bool busy;
  bool clockSynced;
  bool storageReady;
};
inline bool canOffer(Action action, const Availability& state) {
  if (!state.online || state.busy || !state.readGate) return false;
  if (action == Action::Refresh || action == Action::Reconnect) return true;
  if (!state.controlGate) return false;
  if (action == Action::Off) return true;
  return action == Action::On && state.connected && state.confirmedOff &&
         state.fresh && !state.leaseActive && state.clockSynced && state.storageReady;
}
inline const char* displayRelay(bool confirmed, bool on, bool fresh) {
  if (!confirmed) return "UNKNOWN";
  if (!fresh) return "STALE";
  return on ? "ON" : "OFF";
}
inline bool originMatches(const char* boundHost, const char* requestHost,
                          const char* requestOrigin) {
  if (!boundHost || !requestHost || !requestOrigin || !*boundHost) return false;
  const size_t hostLength = strlen(boundHost);
  const bool hostOk = strcmp(requestHost, boundHost) == 0 ||
      (strncmp(requestHost, boundHost, hostLength) == 0 &&
       strcmp(requestHost + hostLength, ":80") == 0);
  if (!hostOk) return false;
  constexpr char prefix[] = "http://";
  if (strncmp(requestOrigin, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* originHost = requestOrigin + sizeof(prefix) - 1;
  return strcmp(originHost, boundHost) == 0 ||
      (strncmp(originHost, boundHost, hostLength) == 0 &&
       strcmp(originHost + hostLength, ":80") == 0);
}
inline bool loginRequestAuthorized(bool originMatches, bool csrfMatches) {
  return originMatches && csrfMatches;
}
constexpr uint32_t kIdleMs = 15 * 60 * 1000U;
constexpr uint32_t kAbsoluteMs = 8 * 60 * 60 * 1000U;
constexpr uint32_t kRateWindowMs = 15 * 60 * 1000U;
inline bool sessionAlive(bool tokenPresent, uint32_t created, uint32_t seen, uint32_t now) {
  return tokenPresent && static_cast<uint32_t>(now - created) < kAbsoluteMs &&
         static_cast<uint32_t>(now - seen) < kIdleMs;
}
inline bool loginAllowed(uint8_t failures, uint32_t since, uint32_t now) {
  return static_cast<uint32_t>(now - since) >= kRateWindowMs || failures < 5;
}
inline bool csrfAuthorized(bool originMatches, bool sessionValid, bool tokenMatches) {
  return originMatches && sessionValid && tokenMatches;
}
inline bool constantEqual(const uint8_t* left, const uint8_t* right, size_t length) {
  uint8_t mismatch = 0;
  for (size_t i = 0; i < length; ++i) mismatch |= left[i] ^ right[i];
  return mismatch == 0;
}
inline bool equalToken(const char* expected, const char* supplied, size_t length) {
  return strlen(expected) == length && strlen(supplied) == length &&
         constantEqual(reinterpret_cast<const uint8_t*>(expected),
                       reinterpret_cast<const uint8_t*>(supplied), length);
}
inline bool cookieHasSession(const char* cookie, const char* session) {
  if (strlen(session) != 64) return false;
  constexpr char prefix[] = "boiler_sid=";
  bool found = false;
  const char* cursor = cookie;
  while (*cursor) {
    while (*cursor == ' ' || *cursor == ';') ++cursor;
    const char* end = strchr(cursor, ';');
    if (!end) end = cursor + strlen(cursor);
    const size_t length = end - cursor;
    if (length == sizeof(prefix) - 1 + 64 &&
        memcmp(cursor, prefix, sizeof(prefix) - 1) == 0 &&
        constantEqual(reinterpret_cast<const uint8_t*>(cursor + sizeof(prefix) - 1),
                      reinterpret_cast<const uint8_t*>(session), 64)) found = true;
    cursor = *end ? end + 1 : end;
  }
  return found;
}
}
