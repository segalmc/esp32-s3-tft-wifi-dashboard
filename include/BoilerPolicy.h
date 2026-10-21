#pragma once
#include <stdint.h>
#include <stddef.h>
#include "BoilerConfig.h"

namespace boiler_policy {
constexpr uint32_t kLeaseVersion = 1;
constexpr uint32_t kShortLeaseVersion = 2;
constexpr uint8_t kMonitorAuthorization = 0xA5;
constexpr uint32_t kMaxOnSeconds = boiler_config::kActive.maxOnSeconds;
constexpr uint32_t kReconcileMs = 30000;
constexpr uint32_t kHeartbeatMs = 10000;
constexpr uint32_t kStaleMs = boiler_config::kActive.staleStateAfterMs;
inline uint32_t reconnectDelayMs(uint8_t failures) {
  const uint8_t shift = failures > 5 ? 5 : failures;
  return 1000U << shift;  // 1, 2, 4, 8, 16, 32 seconds, bounded.
}
inline bool stale(uint32_t lastConfirmed, uint32_t now,
                  const BoilerConfig& config = boiler_config::kActive) {
  return lastConfirmed == 0 ||
         static_cast<uint32_t>(now - lastConfirmed) > config.staleStateAfterMs;
}
constexpr int64_t kMinValidEpoch = 1704067200;  // 2024-01-01 UTC
struct Lease {
  uint32_t version;
  int64_t deadline;
  int64_t created;
};
inline bool validTime(int64_t now) { return now >= kMinValidEpoch && now < 4102444800LL; }
inline bool validLease(const Lease& lease) {
  if (!validTime(lease.created)) return false;
  if (lease.version == kLeaseVersion)
    return lease.deadline == lease.created + 1800;
  return lease.version == kShortLeaseVersion && lease.deadline >= lease.created + 60 &&
         lease.deadline < lease.created + 1800;
}
inline Lease makeLease(int64_t now, const BoilerConfig& config = boiler_config::kActive) {
  return {config.maxOnSeconds == 1800 ? kLeaseVersion : kShortLeaseVersion,
          now + config.maxOnSeconds, now};
}
inline int64_t effectiveDeadline(const Lease& lease,
                                 const BoilerConfig& config = boiler_config::kActive) {
  const int64_t configured = lease.created + config.maxOnSeconds;
  return configured < lease.deadline ? configured : lease.deadline;
}
inline bool due(const Lease& lease, int64_t now, bool ntpValid,
                const BoilerConfig& config = boiler_config::kActive) {
  return validLease(lease) && (!ntpValid || !validTime(now) ||
                                now >= effectiveDeadline(lease, config));
}
inline bool monotonicLeaseDue(uint32_t startedMs, uint32_t nowMs,
                              const BoilerConfig& config = boiler_config::kActive) {
  return startedMs != 0 &&
         static_cast<uint32_t>(nowMs - startedMs) >= config.maxOnSeconds * 1000U;
}
inline uint32_t nextOffRetryMs(uint32_t nowMs,
                                const BoilerConfig& config = boiler_config::kActive) {
  return nowMs + config.offRetryIntervalMs;
}
inline bool offRetryDue(uint32_t nowMs, uint32_t nextMs) {
  return static_cast<int32_t>(nowMs - nextMs) >= 0;
}
inline bool mayTurnOn(bool ntpValid, int64_t now, bool storageReady,
                      const BoilerConfig& config = boiler_config::kActive) {
  return boiler_config::valid(config) && ntpValid && validTime(now) && storageReady;
}
inline bool freshConfirmedOff(bool relayOff, bool connected, uint32_t confirmedMs,
                             uint32_t nowMs,
                             const BoilerConfig& config = boiler_config::kActive) {
  return relayOff && connected && confirmedMs != 0 &&
         static_cast<uint32_t>(nowMs - confirmedMs) <= config.onStateMaxAgeMs;
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
  return store.getUChar(boiler_config::kMonitorKey, 0) == kMonitorAuthorization;
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
  return store.putUChar(boiler_config::kMonitorKey, kMonitorAuthorization) == 1 && monitorArmed(store);
}
template <class Store>
bool disarmMonitor(Store& store) {
  return (!store.isKey(boiler_config::kMonitorKey) ||
          store.remove(boiler_config::kMonitorKey)) && !monitorArmed(store);
}
template <class Store>
bool loadLease(Store& store, Lease& lease) {
  return store.getBytesLength(boiler_config::kLeaseKey) == sizeof(lease) &&
         store.getBytes(boiler_config::kLeaseKey, &lease, sizeof(lease)) == sizeof(lease) &&
         validLease(lease);
}
template <class Store>
bool saveLease(Store& store, const Lease& lease) {
  if (!validLease(lease) || store.isKey(boiler_config::kLeaseKey) ||
      store.putBytes(boiler_config::kLeaseKey, &lease, sizeof(lease)) != sizeof(lease))
    return false;
  Lease check{};
  return loadLease(store, check) && check.created == lease.created &&
         check.deadline == lease.deadline;
}
}
