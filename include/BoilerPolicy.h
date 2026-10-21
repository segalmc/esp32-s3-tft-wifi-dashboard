#pragma once
#include <stdint.h>
#include <stddef.h>

namespace boiler_policy {
constexpr uint32_t kLeaseVersion = 1;
constexpr uint8_t kMonitorAuthorization = 0xA5;
constexpr uint32_t kMaxOnSeconds = 1800;
constexpr uint32_t kReconcileMs = 30000;
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kStaleMs = 45000;
inline uint32_t reconnectDelayMs(uint8_t failures) {
  const uint8_t shift = failures > 5 ? 5 : failures;
  return 1000U << shift;  // 1, 2, 4, 8, 16, 32 seconds, bounded.
}
inline bool stale(uint32_t lastConfirmed, uint32_t now) {
  return lastConfirmed == 0 || static_cast<uint32_t>(now - lastConfirmed) > kStaleMs;
}
constexpr int64_t kMinValidEpoch = 1704067200;  // 2024-01-01 UTC
struct Lease {
  uint32_t version;
  int64_t deadline;
  int64_t created;
};
inline bool validTime(int64_t now) { return now >= kMinValidEpoch && now < 4102444800LL; }
inline bool validLease(const Lease& lease) {
  return lease.version == kLeaseVersion && validTime(lease.created) &&
         lease.deadline == lease.created + kMaxOnSeconds;
}
inline bool due(const Lease& lease, int64_t now, bool ntpValid) {
  return validLease(lease) && (!ntpValid || !validTime(now) || now >= lease.deadline);
}
inline bool mayTurnOn(bool ntpValid, int64_t now, bool storageReady) {
  return ntpValid && validTime(now) && storageReady;
}
inline bool freshConfirmedOff(bool relayOff, bool connected, uint32_t confirmedMs,
                             uint32_t nowMs) {
  return relayOff && connected && confirmedMs != 0 &&
         static_cast<uint32_t>(nowMs - confirmedMs) <= 30000U;
}
inline bool busy(bool pending, uint8_t progress) {
  return pending || progress == 1 || progress == 2 || progress == 3;
}
inline bool reserve(bool& pending, uint8_t progress) {
  if (busy(pending, progress)) return false;
  pending = true;
  return true;
}
inline bool verified(bool requestedOn, bool responseAuthenticated, bool actualOn) {
  return responseAuthenticated && requestedOn == actualOn;
}
template <class Store>
bool monitorArmed(Store& store) {
  return store.getUChar("monitor", 0) == kMonitorAuthorization;
}
struct BootGates {
  bool reads;
  bool control;
};
template <class Store>
BootGates bootGates(Store& store, bool storageReady) {
  return {storageReady && monitorArmed(store), false};
}
template <class Store>
bool armMonitor(Store& store) {
  return store.putUChar("monitor", kMonitorAuthorization) == 1 && monitorArmed(store);
}
template <class Store>
bool disarmMonitor(Store& store) {
  return (!store.isKey("monitor") || store.remove("monitor")) && !monitorArmed(store);
}
template <class Store>
bool loadLease(Store& store, Lease& lease) {
  return store.getBytesLength("lease") == sizeof(lease) &&
         store.getBytes("lease", &lease, sizeof(lease)) == sizeof(lease) &&
         validLease(lease);
}
template <class Store>
bool saveLease(Store& store, const Lease& lease) {
  if (!validLease(lease) || store.putBytes("lease", &lease, sizeof(lease)) != sizeof(lease))
    return false;
  Lease check{};
  return loadLease(store, check) && check.created == lease.created &&
         check.deadline == lease.deadline;
}
}
