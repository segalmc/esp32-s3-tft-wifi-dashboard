#include "DisplayManager.h"

#include <Arduino.h>

#if TFT_HARDWARE_ENABLED
#include <stdio.h>
#include <string.h>

namespace {
constexpr uint16_t kColors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
constexpr const char* kPhaseNames[] = {"red", "green", "blue", "white", "black", "graphics"};
constexpr const char* kLabels[] = {"Chip", "Uptime", "Free heap", "PSRAM total",
                                   "PSRAM free", "Flash", "Resolution", "Refresh"};
constexpr uint32_t kPhaseDurationMs = 1500;
constexpr uint32_t kDashboardIntervalMs = 1000;
constexpr int16_t kFirstRowY = 57;
constexpr int16_t kRowPitch = 27;
}  // namespace
#endif

void DisplayManager::begin() {
#if TFT_HARDWARE_ENABLED
  Serial.printf("TFT: initializing ST7796S at 10 MHz, rotation %d\n", TFT_ROTATION);
  tft_.init();
  tft_.setRotation(TFT_ROTATION);
  Serial.printf("TFT: init commands sent; no readback (MISO disconnected). %d x %d\n",
                tft_.width(), tft_.height());
  showPhase();
  phase_started_ms_ = millis();
#else
  Serial.println("TFT: hardware disabled; SPI and TFT GPIOs untouched");
#endif
}

void DisplayManager::update(uint32_t now_ms) {
#if TFT_HARDWARE_ENABLED
  if (phase_ < 6) {
    if (static_cast<uint32_t>(now_ms - phase_started_ms_) < kPhaseDurationMs) return;
    ++phase_;
    phase_started_ms_ = now_ms;
    if (phase_ < 6) {
      showPhase();
      return;
    }
    Serial.println("TFT: dashboard started");
    drawDashboardFrame();
    last_dashboard_ms_ = now_ms - kDashboardIntervalMs;
  }

  if (static_cast<uint32_t>(now_ms - last_dashboard_ms_) < kDashboardIntervalMs) return;
  last_dashboard_ms_ = now_ms;
  updateDashboard();
#else
  (void)now_ms;
#endif
}

#if TFT_HARDWARE_ENABLED
void DisplayManager::showPhase() {
  Serial.printf("TFT test: %s\n", kPhaseNames[phase_]);
  if (phase_ < 5) tft_.fillScreen(kColors[phase_]);
  else drawGraphics();
}

void DisplayManager::drawGraphics() {
  tft_.fillScreen(TFT_BLACK);
  const int16_t w = tft_.width();
  const int16_t h = tft_.height();
  tft_.drawLine(12, 12, w - 13, h - 13, TFT_YELLOW);
  tft_.drawLine(w - 13, 12, 12, h - 13, TFT_CYAN);
  tft_.drawRect(25, 40, w - 50, h - 80, TFT_GREEN);
  tft_.fillCircle(w / 2, h / 2, 30, TFT_MAGENTA);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  tft_.setTextSize(2);
  tft_.drawString("ESP32-S3 READY", 25, 15);
  tft_.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft_.drawString("ST7796S", 25, h - 34);
}

void DisplayManager::drawDashboardFrame() {
  tft_.fillScreen(TFT_BLACK);
  tft_.setTextSize(2);
  tft_.setTextColor(TFT_CYAN, TFT_BLACK);
  tft_.drawString("ESP32-S3 DIAGNOSTICS", 12, 12);
  tft_.drawFastHLine(12, 42, tft_.width() - 24, TFT_DARKGREY);
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);
  for (uint8_t row = 0; row < 8; ++row) {
    tft_.drawString(kLabels[row], 12, kFirstRowY + row * kRowPitch);
    cached_values_[row][0] = '\0';
  }
}

void DisplayManager::drawValue(uint8_t row, const char* value) {
  if (strcmp(cached_values_[row], value) == 0) return;
  const int16_t value_x = tft_.width() <= 320 ? 160 : 220;
  const int16_t y = kFirstRowY + row * kRowPitch;
  tft_.fillRect(value_x, y, tft_.width() - value_x - 12, 20, TFT_BLACK);
  tft_.setTextColor(TFT_GREEN, TFT_BLACK);
  tft_.drawString(value, value_x, y);
  strncpy(cached_values_[row], value, sizeof(cached_values_[row]) - 1);
  cached_values_[row][sizeof(cached_values_[row]) - 1] = '\0';
}

void DisplayManager::updateDashboard() {
  const uint32_t started_us = micros();
  char value[32];
  drawValue(0, ESP.getChipModel());
  snprintf(value, sizeof(value), "%lu s", static_cast<unsigned long>(millis() / 1000U));
  drawValue(1, value);
  snprintf(value, sizeof(value), "%u KiB", ESP.getFreeHeap() / 1024U);
  drawValue(2, value);
  snprintf(value, sizeof(value), "%u KiB", ESP.getPsramSize() / 1024U);
  drawValue(3, value);
  snprintf(value, sizeof(value), "%u KiB", ESP.getFreePsram() / 1024U);
  drawValue(4, value);
  snprintf(value, sizeof(value), "%u MiB", ESP.getFlashChipSize() / 1048576U);
  drawValue(5, value);
  snprintf(value, sizeof(value), "%d x %d", tft_.width(), tft_.height());
  drawValue(6, value);
  const uint32_t refresh_us = micros() - started_us;
  snprintf(value, sizeof(value), "%lu us", static_cast<unsigned long>(refresh_us));
  drawValue(7, value);
  Serial.printf("TFT dashboard: heap=%u B, PSRAM=%u/%u B, refresh=%lu us\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize(),
                static_cast<unsigned long>(refresh_us));
}
#endif
