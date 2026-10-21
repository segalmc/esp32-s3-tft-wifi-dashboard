#pragma once
#include <stdint.h>
// Deterministic RNG for host fixtures. Firmware uses the ESP32 hardware RNG.
inline uint8_t& esp_random_test_offset() {
  static uint8_t offset = 0;
  return offset;
}
inline uint32_t esp_random() {
  static const char nonce[] = "0123456789abcdef";
  static unsigned index = 0;
  return static_cast<uint8_t>(nonce[(index++) % 16]) + esp_random_test_offset();
}
