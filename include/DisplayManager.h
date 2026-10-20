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
#if NATURE_GLASS_DIAGNOSTICS
  void showDiagnosticStage(uint8_t stage);
  uint8_t diagnostic_stage_ = 0;
#endif

  TFT_eSPI tft_;
  bool background_ready_ = false;
  bool first_dashboard_update_ = true;
  uint8_t phase_ = 0;
  uint32_t phase_started_ms_ = 0;
  uint32_t last_dashboard_ms_ = 0;
  char cached_values_[8][32] = {};
#endif
};
