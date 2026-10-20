#include <Arduino.h>

#include "DisplayManager.h"
#include "SystemMonitor.h"

#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "This firmware requires an ESP32-S3 target"
#endif

SystemMonitor system_monitor;
DisplayManager display_manager;

void setup() {
  Serial.begin(115200);
  Serial.println("Boot: ESP32-S3 Phase 2 diagnostics");
  system_monitor.begin();
  display_manager.begin();
}

void loop() {
  const uint32_t now_ms = millis();
  system_monitor.update(now_ms);
  display_manager.update(now_ms);
}
