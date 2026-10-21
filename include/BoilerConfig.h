#pragma once
#include <stdint.h>

// Application-only safety settings. Change kActive at build time, never from
// Web, Serial, NVS, or a generic Tuya client. Validation permits tightening
// the proven limits but does not permit disabling NTP or verified persistence.
struct BoilerConfig {
  enum class ClockRequirement : uint8_t { SynchronizedNtp = 1 };
  enum class Persistence : uint8_t { VerifiedNvsBeforeOn = 1 };

  uint32_t maxOnSeconds;
  uint32_t offRetryIntervalMs;
  uint32_t onStateMaxAgeMs;
  uint32_t staleStateAfterMs;
  uint8_t verificationReadAttempts;
  uint32_t verificationReadDelayMs;
  ClockRequirement clock;
  Persistence persistence;

  constexpr BoilerConfig(uint32_t maxOn = 1800, uint32_t offRetry = 15000,
      uint32_t onAge = 30000, uint32_t staleAfter = 45000,
      uint8_t verifyReads = 3, uint32_t verifyDelay = 800,
      ClockRequirement clockMode = ClockRequirement::SynchronizedNtp,
      Persistence storageMode = Persistence::VerifiedNvsBeforeOn)
      : maxOnSeconds(maxOn), offRetryIntervalMs(offRetry), onStateMaxAgeMs(onAge),
        staleStateAfterMs(staleAfter), verificationReadAttempts(verifyReads),
        verificationReadDelayMs(verifyDelay), clock(clockMode), persistence(storageMode) {}
};

namespace boiler_config {
constexpr uint32_t kConfiguredMaxOnSeconds = 1800;
constexpr uint32_t kConfiguredOffRetryMs = 15000;
constexpr uint32_t kConfiguredOnStateMaxAgeMs = 30000;
constexpr uint32_t kConfiguredStaleAfterMs = 45000;
constexpr uint8_t kConfiguredVerificationReads = 3;
constexpr uint32_t kConfiguredVerificationDelayMs = 800;
constexpr BoilerConfig kActive{kConfiguredMaxOnSeconds, kConfiguredOffRetryMs,
    kConfiguredOnStateMaxAgeMs, kConfiguredStaleAfterMs,
    kConfiguredVerificationReads, kConfiguredVerificationDelayMs};
constexpr bool valid(const BoilerConfig& config) {
  return config.maxOnSeconds >= 60 && config.maxOnSeconds <= 1800 &&
      config.offRetryIntervalMs >= 1000 && config.offRetryIntervalMs <= 15000 &&
      config.onStateMaxAgeMs >= 1000 && config.onStateMaxAgeMs <= 30000 &&
      config.staleStateAfterMs >= config.onStateMaxAgeMs &&
      config.staleStateAfterMs <= 45000 &&
      config.verificationReadAttempts >= 3 && config.verificationReadAttempts <= 5 &&
      config.verificationReadDelayMs >= 100 && config.verificationReadDelayMs <= 800 &&
      config.clock == BoilerConfig::ClockRequirement::SynchronizedNtp &&
      config.persistence == BoilerConfig::Persistence::VerifiedNvsBeforeOn;
}
static_assert(kConfiguredMaxOnSeconds >= 60 && kConfiguredMaxOnSeconds <= 1800,
              "Unsafe boiler maximum ON duration");
static_assert(kConfiguredOffRetryMs >= 1000 && kConfiguredOffRetryMs <= 15000,
              "Unsafe boiler OFF retry interval");
static_assert(kConfiguredOnStateMaxAgeMs >= 1000 && kConfiguredOnStateMaxAgeMs <= 30000,
              "Unsafe boiler ON state freshness");
static_assert(kConfiguredStaleAfterMs >= kConfiguredOnStateMaxAgeMs &&
              kConfiguredStaleAfterMs <= 45000, "Unsafe boiler stale threshold");
static_assert(kConfiguredVerificationReads >= 3 && kConfiguredVerificationReads <= 5,
              "Unsafe boiler verification read count");
static_assert(kConfiguredVerificationDelayMs >= 100 && kConfiguredVerificationDelayMs <= 800,
              "Unsafe boiler verification delay");
constexpr const char* kNvsNamespace = "boiler";
constexpr const char* kLeaseKey = "lease";
constexpr const char* kMonitorKey = "monitor";
}  // namespace boiler_config
