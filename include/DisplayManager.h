#pragma once

#include <stdint.h>
#include "TftConfig.h"

#if TFT_HARDWARE_ENABLED
#include "NetworkManager.h"
#include "TuyaControl.h"
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
  void updateOta(NetworkManager::OtaSnapshot ota, const char* password,
                 bool reveal, uint32_t now_ms);
  void updateBoiler(TuyaControl::Snapshot state);
  uint32_t lastRefreshUs() const { return last_refresh_us_; }
#endif

 private:
#if TFT_HARDWARE_ENABLED
  void showPhase();
  void drawGraphics();
  void drawDashboardFrame();
  void updateDashboard();
  void drawValue(uint8_t row, const char* value);
  void drawBoilerValue();
  void restoreValueRegion(uint8_t row);
  void streamAssetRegion(const uint16_t* pixels, int16_t x, int16_t y,
                         int16_t width, int16_t height);
  void drawNetworkStatus();
  void drawSetupPanel(bool visible);
  void drawClockPanel(const char* time_text, const char* weekday,
                      const char* date_text);
  void restoreTopPanelRegion();
  void drawOtaPanel(NetworkManager::OtaPhase phase, uint8_t percent,
                    uint8_t error, bool show_password);
#if NATURE_GLASS_DIAGNOSTICS
  void showDiagnosticStage(uint8_t stage);
  uint8_t diagnostic_stage_ = 0;
#endif

  TFT_eSPI tft_;
  bool background_ready_ = false;
  bool first_dashboard_update_ = true;
  bool dashboard_ready_ = false;
  bool initial_password_shown_ = false;
  bool setup_panel_visible_ = false;
  bool network_status_dirty_ = true;
  bool clock_was_synced_ = false;
  bool clock_visible_ = false;
  bool clock_dirty_ = true;
  bool ota_panel_visible_ = false;
  bool ota_secret_visible_ = false;
  NetworkManager::OtaPhase ota_drawn_phase_ = NetworkManager::OtaPhase::Idle;
  uint8_t ota_drawn_percent_ = 0;
  uint8_t ota_drawn_error_ = 0;
  uint32_t ota_last_error_event_ = 0;
  char ota_password_[21] = {};
  uint32_t ota_reveal_until_ms_ = 0;
  uint32_t ota_error_until_ms_ = 0;
  char network_status_[16] = "WIFI STARTING";
  char cached_clock_time_[6] = {};
  char cached_clock_date_[16] = {};
  uint8_t cached_weekday_ = 255;
  char setup_password_[17] = {};
  uint32_t reveal_until_ms_ = 0;
  uint32_t last_clock_sample_ms_ = 0;
  uint32_t last_refresh_us_ = 0;
  uint8_t phase_ = 0;
  uint32_t phase_started_ms_ = 0;
  uint32_t last_dashboard_ms_ = 0;
  char cached_values_[8][32] = {};
  TuyaControl::Snapshot boiler_state_{};
#endif
};
