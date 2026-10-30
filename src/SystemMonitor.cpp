#include "SystemMonitor.h"

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"

void SystemMonitor::begin() {
  started_ms_ = millis();
  last_report_ms_ = started_ms_;
  Serial.println("ESP32-S3 Phase 1 hardware check");
  Serial.printf("Boot reset reason: %u\n",
                static_cast<unsigned>(esp_reset_reason()));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());
  Serial.printf("Flash size: %lu bytes\n", static_cast<unsigned long>(ESP.getFlashChipSize()));
  const bool psram_available = psramFound();
  Serial.printf("PSRAM available: %s\n", psram_available ? "yes" : "no");
  Serial.printf("PSRAM size: %lu bytes\n", static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("Free heap: %lu bytes\n", static_cast<unsigned long>(ESP.getFreeHeap()));
  Serial.printf("Free PSRAM: %lu bytes\n", static_cast<unsigned long>(ESP.getFreePsram()));

#if RGB_LED_GPIO >= 0
  Serial.printf("RGB LED test enabled on GPIO %d\n", RGB_LED_GPIO);
  neopixelWrite(RGB_LED_GPIO, 0, 0, 0);
#else
  Serial.println("RGB LED test disabled: GPIO and LED type unconfirmed");
#endif
}

void SystemMonitor::update(uint32_t now_ms) {
  // Native USB can enumerate after setup(). Repeat the chip report without
  // delaying the rest of the firmware.
  if (!boot_replayed_ && static_cast<uint32_t>(now_ms - started_ms_) >= 2000U) {
    boot_replayed_ = true;
    Serial.printf("Boot report: %s | Flash: %lu bytes | PSRAM: %lu bytes | Free heap: %lu bytes\n",
                  ESP.getChipModel(), static_cast<unsigned long>(ESP.getFlashChipSize()), static_cast<unsigned long>(ESP.getPsramSize()),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
  if (static_cast<uint32_t>(now_ms - last_report_ms_) < 30000U) return;
  last_report_ms_ = now_ms;
  seconds_ = now_ms / 1000U;
  Serial.printf("Uptime: %lu s | Flash: %lu MB | PSRAM: %lu MB | PSRAM OK: %s | Free heap: %lu B | Free PSRAM: %lu B\n",
                static_cast<unsigned long>(seconds_),
                static_cast<unsigned long>(ESP.getFlashChipSize()) / 1024U / 1024U,
                static_cast<unsigned long>(ESP.getPsramSize()) / 1024U / 1024U,
                psramFound() ? "YES" : "NO", static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()));

#if RGB_LED_GPIO >= 0
  // Red, green, blue, off; advance once per second without blocking.
  switch (seconds_ % 4U) {
    case 0: neopixelWrite(RGB_LED_GPIO, 255, 0, 0); break;
    case 1: neopixelWrite(RGB_LED_GPIO, 0, 255, 0); break;
    case 2: neopixelWrite(RGB_LED_GPIO, 0, 0, 255); break;
    default: neopixelWrite(RGB_LED_GPIO, 0, 0, 0); break;
  }
#endif
}

