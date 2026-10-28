#pragma once

#include <stdint.h>
#include "TftConfig.h"

#if TFT_HARDWARE_ENABLED
#include "NetworkManager.h"
#include "TuyaControl.h"
#include "LocalHttpTelemetry.h"
#include "TemperatureHistory.h"
#include <TFT_eSPI.h>
#endif

class DisplayManager {
 public:
  void begin();
  void update(uint32_t now_ms);
#if TFT_HARDWARE_ENABLED
  void updateNetwork(const char* status, bool ap_active, bool initial_setup,
                     const char* ap_password, bool reveal, uint32_t now_ms);
  void updateClock(bool ntp_synced, uint32_t now_ms);
  void updateOta(NatureNetworkManager::OtaSnapshot ota, const char* password,
                 bool reveal, uint32_t now_ms);
  void updateBoiler(TuyaControl::Snapshot state);
  void updateTelemetry(LocalHttpTelemetry::Snapshot state);
  uint32_t lastRefreshUs() const { return last_refresh_us_; }
#endif

 private:
#if TFT_HARDWARE_ENABLED
  void drawDashboardFrame();
  void drawTelemetryPage(bool chart);
  void streamAssetRegion(const uint16_t* pixels, int16_t x, int16_t y,
                         int16_t width, int16_t height);
  void drawSetupPanel(bool visible);
  void restoreTopPanelRegion();
  void drawOtaPanel(NatureNetworkManager::OtaPhase phase, uint8_t percent,
                    uint8_t error, bool show_password);
  TFT_eSPI tft_;
  bool background_ready_ = false;
  bool dashboard_ready_ = false;
  bool initial_password_shown_ = false;
  bool setup_panel_visible_ = false;
  bool ota_panel_visible_ = false;
  bool ota_secret_visible_ = false;
  NatureNetworkManager::OtaPhase ota_drawn_phase_ = NatureNetworkManager::OtaPhase::Idle;
  uint8_t ota_drawn_percent_ = 0;
  uint8_t ota_drawn_error_ = 0;
  uint32_t ota_last_error_event_ = 0;
  char ota_password_[21] = {};
  uint32_t ota_reveal_until_ms_ = 0;
  uint32_t ota_error_until_ms_ = 0;
  char network_status_[16] = "WIFI STARTING";
  char setup_password_[17] = {};
  uint32_t reveal_until_ms_ = 0;
  uint32_t last_refresh_us_ = 0;
  uint32_t last_dashboard_ms_ = 0;
  TuyaControl::Snapshot boiler_state_{};
  LocalHttpTelemetry::Snapshot telemetry_state_{};
  TemperatureHistory* history_ = nullptr;
  bool first_chart_refresh_ = true;
  char telemetry_lines_[12][64] = {};
  bool chart_dirty_ = true;
  int64_t chart_minute_ = -1;
  uint32_t last_chart_draw_ms_ = 0;
  int64_t last_history_expire_second_ = 0;
  uint32_t last_telemetry_sequence_ = 0;
  uint64_t last_telemetry_boot_ = 0;
#endif
};
