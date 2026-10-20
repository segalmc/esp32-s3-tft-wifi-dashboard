#include "SystemMonitor.h"

#include <Arduino.h>
#include "config.h"

void SystemMonitor::begin() {
  started_ms_ = millis();
  last_report_ms_ = started_ms_;
  Serial.println("ESP32-S3 Phase 1 hardware check");
  Serial.printf("Chip model: %s\n", ESP.getChipModel());
  Serial.printf("Flash size: %u bytes\n", ESP.getFlashChipSize());
  const bool psram_available = psramFound();
  Serial.printf("PSRAM available: %s\n", psram_available ? "yes" : "no");
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());

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
    Serial.printf("Boot report: %s | Flash: %u bytes | PSRAM: %u bytes | Free heap: %u bytes\n",
                  ESP.getChipModel(), ESP.getFlashChipSize(), ESP.getPsramSize(),
                  ESP.getFreeHeap());
  }
  if (static_cast<uint32_t>(now_ms - last_report_ms_) < 1000U) return;
  last_report_ms_ += 1000U;
  ++seconds_;
  Serial.printf("Uptime: %lu s | Flash: %u MB | PSRAM: %u MB | PSRAM OK: %s | Free heap: %u B | Free PSRAM: %u B\n",
                static_cast<unsigned long>(seconds_),
                ESP.getFlashChipSize() / 1024U / 1024U,
                ESP.getPsramSize() / 1024U / 1024U,
                psramFound() ? "YES" : "NO", ESP.getFreeHeap(),
                ESP.getFreePsram());

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

