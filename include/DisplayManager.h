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

  TFT_eSPI tft_;
  uint8_t phase_ = 0;
  uint32_t phase_started_ms_ = 0;
  uint32_t last_dashboard_ms_ = 0;
  char cached_values_[8][32] = {};
#endif
};
