#pragma once

#include <stdint.h>
#include "TftConfig.h"

#if TFT_HARDWARE_ENABLED
#include <TFT_eSPI.h>
#endif

class DisplayManager {
 public:
  void begin();
  void update(uint32_t now_ms);
#if TFT_HARDWARE_ENABLED
  void updateNetwork(const char* status, bool ap_active, bool initial_setup,
                     const char* ap_password, bool reveal, uint32_t now_ms);
  uint32_t lastRefreshUs() const { return last_refresh_us_; }
#endif

 private:
#if TFT_HARDWARE_ENABLED
  void showPhase();
  void drawGraphics();
  void drawDashboardFrame();
  void updateDashboard();
  void drawValue(uint8_t row, const char* value);
  void restoreValueRegion(uint8_t row);
  void streamAssetRegion(const uint16_t* pixels, int16_t x, int16_t y,
                         int16_t width, int16_t height);
  void drawNetworkStatus();
  void drawSetupPanel(bool visible);
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
  char network_status_[16] = "WIFI STARTING";
  char setup_password_[17] = {};
  uint32_t reveal_until_ms_ = 0;
  uint32_t last_refresh_us_ = 0;
  uint8_t phase_ = 0;
  uint32_t phase_started_ms_ = 0;
  uint32_t last_dashboard_ms_ = 0;
  char cached_values_[8][32] = {};
#endif
};
