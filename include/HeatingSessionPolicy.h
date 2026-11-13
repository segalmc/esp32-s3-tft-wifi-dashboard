#pragma once
#include "BoilerPolicy.h"
#include <math.h>
#include <string.h>

namespace heating_session {
constexpr uint32_t kDefaultSeconds = 1800;
constexpr uint32_t kDefaultTargetC = 50;
constexpr uint32_t kTemperatureMaxAgeMs = 15000;
constexpr const char* kSessionKey = "session";
inline bool validDuration(uint32_t seconds) {
  return seconds == 900 || seconds == 1800 || seconds == 2700 || seconds == 3600;
}
inline bool parseSettings(const char* secondsText, const char* targetText,
                          uint32_t& seconds, uint32_t& target) {
  seconds = 0; target = 0;
  if (!secondsText || !targetText) return false;
  const uint32_t durations[] = {900, 1800, 2700, 3600};
  for (uint32_t value : durations) {
    const char* text = value == 900 ? "900" : value == 1800 ? "1800" : value == 2700 ? "2700" : "3600";
    if (strcmp(secondsText, text) == 0) seconds = value;
  }
  if (strcmp(targetText, "40") == 0) target = 40;
  else if (strcmp(targetText, "50") == 0) target = 50;
  else if (strcmp(targetText, "60") == 0) target = 60;
  return seconds != 0 && target != 0;
}
inline bool validTarget(uint32_t target) { return target == 40 || target == 50 || target == 60; }
// Longer sessions are modeled and tested, but physical approval is deliberately absent.
inline bool physicallyAllowed(uint32_t seconds) {
  return validDuration(seconds) && seconds <= boiler_config::kActive.maxOnSeconds;
}
inline bool freshTemperature(bool valid, float c, uint32_t received, uint32_t now) {
  return valid && isfinite(c) && c >= -50 && c <= 125 &&
         static_cast<uint32_t>(now - received) <= kTemperatureMaxAgeMs;
}
inline bool reached(uint32_t target, bool fresh, float boilerC) {
  return validTarget(target) && fresh && isfinite(boilerC) && boilerC >= target;
}
enum class Stop : uint8_t { None, Timer, Target, Manual, Recovery, Clock };
inline const char* reason(Stop stop) {
  switch (stop) {
    case Stop::Timer: return "timer";
    case Stop::Target: return "target";
    case Stop::Manual: return "manual";
    case Stop::Recovery: return "recovery";
    case Stop::Clock: return "clock";
    default: return "none";
  }
}
// Stable precedence when both triggers arrive in the same controller iteration.
inline Stop stopReason(bool timer, bool target) {
  return timer ? Stop::Timer : target ? Stop::Target : Stop::None;
}
inline bool elapsed(uint32_t started, uint32_t now, uint32_t seconds) {
  return static_cast<uint32_t>(now - started) >= seconds * 1000U;
}
struct Record {
  uint32_t version;
  uint32_t duration;
  uint32_t target;
  uint32_t checksum;
  int64_t created;
  int64_t deadline;
};
inline uint32_t checksum(const Record& r) {
  return 0x48575331U ^ r.version ^ r.duration ^ r.target ^
      static_cast<uint32_t>(r.created) ^ static_cast<uint32_t>(r.created >> 32) ^
      static_cast<uint32_t>(r.deadline) ^ static_cast<uint32_t>(r.deadline >> 32);
}
inline Record makeRecord(const boiler_policy::Lease& lease, uint32_t target) {
  Record r{1, static_cast<uint32_t>(lease.deadline - lease.created), target, 0,
           lease.created, lease.deadline};
  r.checksum = checksum(r);
  return r;
}
inline bool validRecord(const Record& r, const boiler_policy::Lease& lease) {
  return r.version == 1 && validDuration(r.duration) && validTarget(r.target) &&
      r.checksum == checksum(r) && r.created == lease.created &&
      r.deadline == lease.deadline && r.deadline - r.created == r.duration;
}
template<class Store>
bool loadRecord(Store& store, Record& record, const boiler_policy::Lease& lease) {
  return store.getBytesLength(kSessionKey) == sizeof(record) &&
      store.getBytes(kSessionKey, &record, sizeof(record)) == sizeof(record) &&
      validRecord(record, lease);
}
template<class Store>
bool saveRecord(Store& store, const Record& record, const boiler_policy::Lease& lease) {
  if (!validRecord(record, lease) || store.isKey(boiler_config::kLeaseKey) ||
      store.putBytes(kSessionKey, &record, sizeof(record)) != sizeof(record)) return false;
  Record check{};
  return loadRecord(store, check, lease) && check.checksum == record.checksum;
}
} // namespace heating_session
