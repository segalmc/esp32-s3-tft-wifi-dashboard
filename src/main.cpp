#include <Arduino.h>
#include <string.h>

#include "DisplayManager.h"
#include "SystemMonitor.h"
#if TFT_HARDWARE_ENABLED
#include "NetworkManager.h"
#include "TuyaControl.h"
#include "BoilerWeb.h"
#include "LocalWssServer.h"
#endif

#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "This firmware requires an ESP32-S3 target"
#endif

SystemMonitor system_monitor;
DisplayManager display_manager;
#if TFT_HARDWARE_ENABLED
NatureNetworkManager network_manager;
TuyaControl tuya_control;
BoilerWeb boiler_web;
LocalWssServer local_wss;
uint32_t last_loop_ms = 0;
uint32_t stats_started_ms = 0;
uint32_t max_loop_gap_ms = 0;
uint32_t max_dashboard_us = 0;
#endif

void setup() {
  Serial.begin(115200);
  Serial.println("Boot: ESP32-S3 Phase 2 diagnostics");
  system_monitor.begin();
  display_manager.begin();
#if TFT_HARDWARE_ENABLED
  network_manager.begin();
  tuya_control.begin(&network_manager);
  network_manager.attachTuya(&tuya_control);
  boiler_web.begin(&network_manager, &tuya_control);
  local_wss.begin(&network_manager);
#endif
}

void loop() {
  const uint32_t now_ms = millis();
  system_monitor.update(now_ms);
#if TFT_HARDWARE_ENABLED
  display_manager.updateBoiler(tuya_control.snapshot());
#endif
  display_manager.update(now_ms);
#if TFT_HARDWARE_ENABLED
  network_manager.update();
  const auto wifi = network_manager.snapshot();
  const char* wifi_label = "WIFI OFFLINE";
  switch (wifi.state) {
    case NatureNetworkManager::State::Setup: wifi_label = "WIFI SETUP"; break;
    case NatureNetworkManager::State::Connecting: wifi_label = "WIFI JOINING"; break;
    case NatureNetworkManager::State::Online: wifi_label = "WIFI ONLINE"; break;
    case NatureNetworkManager::State::RetryWait: wifi_label = "WIFI RETRY"; break;
    case NatureNetworkManager::State::Error: wifi_label = "WIFI ERROR"; break;
  }
  char setup_password[17] = {};
  if (wifi.ap_active) network_manager.copySetupPassword(setup_password, sizeof(setup_password));
  display_manager.updateNetwork(wifi_label, wifi.ap_active, wifi.initial_setup,
                                setup_password, network_manager.takeRevealRequest(), now_ms);
  display_manager.updateClock(wifi.clock_synced, now_ms);
  memset(setup_password, 0, sizeof(setup_password));
  const bool ota_reveal = network_manager.takeOtaRevealRequest();
  char ota_password[21] = {};
  if (ota_reveal) network_manager.copyOtaPassword(ota_password, sizeof(ota_password));
  display_manager.updateOta(network_manager.otaSnapshot(), ota_password, ota_reveal, now_ms);
  memset(ota_password, 0, sizeof(ota_password));

  if (last_loop_ms) max_loop_gap_ms = max(max_loop_gap_ms, now_ms - last_loop_ms);
  last_loop_ms = now_ms;
  max_dashboard_us = max(max_dashboard_us, display_manager.lastRefreshUs());
  if (now_ms - stats_started_ms >= 30000U) {
    Serial.printf("Performance: AP=%s loop_gap_max=%lu ms dashboard_max=%lu us heap=%lu B PSRAM=%lu B\n",
                  wifi.ap_active ? "on" : "off", static_cast<unsigned long>(max_loop_gap_ms),
                  static_cast<unsigned long>(max_dashboard_us), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()));
    stats_started_ms = now_ms;
    max_loop_gap_ms = 0;
    max_dashboard_us = 0;
  }
#endif
}
