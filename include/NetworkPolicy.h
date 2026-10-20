#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace network_policy {
constexpr uint32_t kRecoveryDelayMs = 300000;
constexpr uint32_t kMaxRetryMs = 60000;

inline bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

inline uint32_t nextRetryDelay(uint32_t current) {
  return current >= kMaxRetryMs / 2 ? kMaxRetryMs : current * 2;
}

inline bool recoveryDue(bool has_saved, bool link_and_dhcp, uint32_t now,
                        uint32_t offline_since) {
  return has_saved && !link_and_dhcp && due(now, offline_since + kRecoveryDelayMs);
}

template <typename Credentials, typename Persist>
bool commitAfterPersistence(Credentials& saved, const Credentials& candidate,
                            Persist persist) {
  if (!persist(candidate)) return false;
  saved = candidate;
  return true;
}

inline bool validHomeCredentials(const char* ssid, size_t ssid_size,
                                 const char* password, size_t password_size) {
  const size_t ssid_len = strnlen(ssid, ssid_size);
  const size_t pass_len = strnlen(password, password_size);
  if (ssid_len < 1 || ssid_len > 32 || pass_len < 8 || pass_len > 63) return false;
  for (size_t i = 0; i < ssid_len; ++i) {
    const uint8_t c = static_cast<uint8_t>(ssid[i]);
    if (c < 0x20 || c == 0x7F) return false;
  }
  for (size_t i = 0; i < pass_len; ++i) {
    const uint8_t c = static_cast<uint8_t>(password[i]);
    if (c < 0x20 || c > 0x7E) return false;
  }
  return true;
}
}  // namespace network_policy
