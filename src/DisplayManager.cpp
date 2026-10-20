#include "DisplayManager.h"

#include <Arduino.h>

#if TFT_HARDWARE_ENABLED
#include "BackgroundAsset.h"
#include <stdio.h>
#include <string.h>

#ifndef LOAD_FONT2
#error "Nature Glass values require TFT_eSPI font 2"
#endif

#ifndef NATURE_GLASS_DIAGNOSTICS
#define NATURE_GLASS_DIAGNOSTICS 0
#endif

namespace {
constexpr uint16_t kColors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
constexpr const char* kPhaseNames[] = {"red", "green", "blue", "white", "black", "graphics"};
constexpr const char* kLabels[] = {"CHIP", "UPTIME", "FREE HEAP", "PSRAM TOTAL",
                                   "PSRAM FREE", "FLASH", "RESOLUTION", "REFRESH"};
constexpr uint32_t kPhaseDurationMs = 1500;
constexpr uint32_t kDashboardIntervalMs = 1000;
constexpr uint16_t kNavy = 0x08C9;
constexpr uint16_t kTurquoise = 0x4EBA;
constexpr uint16_t kOrange = 0xFD2C;
constexpr int16_t kValueWidth = 120;
constexpr int16_t kValueHeight = 20;
static_assert(sizeof(kBackgroundPixels) == 320U * 480U * 2U,
              "RGB565 asset must contain exactly 320 x 480 pixels");
static_assert(kBackgroundWidth % 2 == 0 && kValueWidth % 2 == 0,
              "pushImage row and restore widths must be even");
#if NATURE_GLASS_DIAGNOSTICS
constexpr uint32_t kDiagnosticDurationMs = 5000;
static_assert(sizeof(kPhotoOnlyPixels) == 320U * 480U * 2U,
              "Photo diagnostic must contain exactly 320 x 480 pixels");
#define TEST_ROW(a, b) a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b,b
#define TEST_EIGHT(a, b) TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b),TEST_ROW(a,b)
alignas(4) const uint16_t kKnownImage[32 * 32] PROGMEM = {
    TEST_EIGHT(TFT_RED, TFT_GREEN), TEST_EIGHT(TFT_RED, TFT_GREEN),
    TEST_EIGHT(TFT_BLUE, TFT_WHITE), TEST_EIGHT(TFT_BLUE, TFT_WHITE)};
#undef TEST_EIGHT
#undef TEST_ROW
#endif
int16_t cardX(uint8_t index) { return 12 + (index % 2) * 153; }
int16_t cardY(uint8_t index) { return 188 + (index / 2) * 67; }
int16_t valueX(uint8_t index) { return (cardX(index) + 13) & ~1; }
uint32_t assetChecksum(const uint16_t* pixels) {
  uint32_t checksum = 0;
  for (size_t i = 0; i < kBackgroundWidth * kBackgroundHeight; ++i) {
    checksum += pgm_read_word(&pixels[i]);
  }
  return checksum;
}
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
#if NATURE_GLASS_DIAGNOSTICS
    diagnostic_stage_ = 0;
    showDiagnosticStage(0);
    return;
#else
    Serial.println("TFT: dashboard started");
    drawDashboardFrame();
    last_dashboard_ms_ = now_ms - kDashboardIntervalMs;
#endif
  }

#if NATURE_GLASS_DIAGNOSTICS
  if (diagnostic_stage_ < 4) {
    if (static_cast<uint32_t>(now_ms - phase_started_ms_) < kDiagnosticDurationMs) return;
    ++diagnostic_stage_;
    phase_started_ms_ = now_ms;
    if (diagnostic_stage_ < 4) {
      showDiagnosticStage(diagnostic_stage_);
      return;
    }
    Serial.println("TFT: dashboard started after image diagnostics");
    drawDashboardFrame();
    last_dashboard_ms_ = now_ms - kDashboardIntervalMs;
  }
#endif

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

#if NATURE_GLASS_DIAGNOSTICS
void DisplayManager::showDiagnosticStage(uint8_t stage) {
  if (stage == 0 || stage == 1) {
    tft_.setSwapBytes(true);
    const bool photo_only = stage == 0;
    Serial.printf("TFT image diagnostic %u/4: %s, 320x480 single pushImage\n",
                  static_cast<unsigned>(stage + 1),
                  photo_only ? "photo only" : "photo plus glass cards");
    const uint16_t* pixels = photo_only ? kPhotoOnlyPixels : kBackgroundPixels;
    const uint32_t expected = photo_only ? kPhotoOnlyChecksum : kBackgroundChecksum;
    Serial.printf("TFT image diagnostic: asset checksum %s\n",
                  assetChecksum(pixels) == expected ? "OK" : "FAILED");
    const uint32_t started_us = micros();
    tft_.pushImage(0, 0, kBackgroundWidth, kBackgroundHeight,
                   pixels);
    Serial.printf("TFT image diagnostic: draw=%lu us\n",
                  static_cast<unsigned long>(micros() - started_us));
    return;
  }
  tft_.fillScreen(TFT_BLACK);
  tft_.setTextFont(2);
  tft_.setTextSize(1);
  tft_.setTextColor(TFT_WHITE);
  if (stage == 2) {
    Serial.println("TFT image diagnostic 3/4: fillRect RGB565 reference blocks");
    tft_.drawString("RGB565 FILL REFERENCE", 20, 25);
    const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE};
    const char* names[] = {"RED", "GREEN", "BLUE", "WHITE"};
    for (uint8_t i = 0; i < 4; ++i) {
      const int16_t x = 17 + i * 76;
      tft_.fillRect(x, 125, 58, 96, colors[i]);
      tft_.drawString(names[i], x, 237);
    }
    tft_.drawString("No pushImage used", 20, 305);
  } else {
    Serial.println("TFT image diagnostic 4/4: same flash image, swap off at left / on at right");
    tft_.drawString("FLASH IMAGE BYTE ORDER", 20, 25);
    tft_.drawString("SWAP OFF", 31, 100);
    tft_.drawString("SWAP ON", 190, 100);
    for (uint8_t side = 0; side < 2; ++side) {
      tft_.setSwapBytes(side == 1);
      const int16_t x = side == 0 ? 32 : 191;
      for (uint8_t ty = 0; ty < 3; ++ty) {
        for (uint8_t tx = 0; tx < 3; ++tx) {
          tft_.pushImage(x + tx * 32, 150 + ty * 32, 32, 32, kKnownImage);
        }
      }
    }
    tft_.drawString("Expected: red/green above blue/white", 15, 300);
    tft_.setSwapBytes(true);
  }
}
#endif

void DisplayManager::drawDashboardFrame() {
  const uint32_t started_us = micros();
  // The generated C array contains native RGB565 words. TFT_eSPI's ESP32-S3
  // pushImage path sends byte-swapped words by default; fillRect/drawString
  // do not use that path. Apply this to both the full image and value restores.
  tft_.setSwapBytes(true);
  background_ready_ = tft_.width() == kBackgroundWidth &&
                      tft_.height() == kBackgroundHeight;
  if (background_ready_) {
    background_ready_ = assetChecksum(kBackgroundPixels) == kBackgroundChecksum;
  }
  if (background_ready_) {
    // One window and one library image call. Its const overload copies one
    // flash row at a time to a small stack buffer before transmitting it.
    tft_.pushImage(0, 0, kBackgroundWidth, kBackgroundHeight,
                   kBackgroundPixels);
  } else {
    Serial.println("TFT: background unavailable; using solid glass cards");
    tft_.fillScreen(kNavy);
    tft_.fillRoundRect(12, 12, 296, 66, 14, 0x11E8);
    for (uint8_t row = 0; row < 8; ++row) {
      tft_.fillRoundRect(cardX(row), cardY(row), 144, 59, 12, 0x11E8);
      tft_.drawRoundRect(cardX(row), cardY(row), 144, 59, 12, kTurquoise);
      tft_.drawFastHLine(cardX(row) + 13, cardY(row) + 13, 18, kTurquoise);
    }
  }

  tft_.setTextFont(1);
  tft_.setTextSize(1);
  tft_.setTextColor(kTurquoise);
  tft_.drawString("SYSTEM / FIELD NOTES", 25, 22);
  tft_.setTextFont(2);
  tft_.setTextSize(2);
  tft_.setTextColor(TFT_WHITE);
  tft_.drawString("ESP32-S3", 24, 42);
  tft_.setTextSize(1);
  tft_.setTextFont(1);
  tft_.setTextColor(TFT_WHITE);
  tft_.drawString("DEVICE DIAGNOSTICS", 18, 164);
  tft_.setTextColor(kOrange);
  tft_.drawString("MOUNTAIN LAKE  /  SYSTEM HEALTH", 25, 455);
  for (uint8_t row = 0; row < 8; ++row) {
    tft_.setTextColor(kTurquoise);
    tft_.drawString(kLabels[row], cardX(row) + 13, cardY(row) + 18);
    cached_values_[row][0] = '\0';
  }
  Serial.printf("TFT: frame=%lu us, heap=%u B, PSRAM=%u/%u B, photo=%s\n",
                static_cast<unsigned long>(micros() - started_us), ESP.getFreeHeap(),
                ESP.getFreePsram(), ESP.getPsramSize(),
                background_ready_ ? "yes" : "fallback");
}

void DisplayManager::streamAssetRegion(const uint16_t* pixels, int16_t x,
                                        int16_t y, int16_t width,
                                        int16_t height) {
  // The asset stride is always 320, even for a narrow card region. A single
  // address window keeps adjacent scan lines contiguous on the display.
  alignas(4) uint16_t row[kBackgroundWidth];
  tft_.startWrite();
  tft_.setAddrWindow(x, y, width, height);
  for (int16_t line = 0; line < height; ++line) {
    const size_t source = static_cast<size_t>(y + line) * kBackgroundWidth + x;
    for (int16_t column = 0; column < width; ++column) {
      row[column] = pgm_read_word(&pixels[source + column]);
    }
    tft_.pushPixels(row, width);
  }
  tft_.endWrite();
}

void DisplayManager::restoreValueRegion(uint8_t row) {
  const int16_t x = valueX(row);
  const int16_t y = cardY(row) + 32;
  if (background_ready_) {
    streamAssetRegion(kBackgroundPixels, x, y, kValueWidth, kValueHeight);
  } else {
    tft_.fillRect(x, y, kValueWidth, kValueHeight, 0x11E8);
  }
}

void DisplayManager::drawValue(uint8_t row, const char* value) {
  if (strcmp(cached_values_[row], value) == 0) {
    if (first_dashboard_update_) {
      Serial.printf("TFT value %u unchanged unexpectedly: '%s'\n",
                    static_cast<unsigned>(row), value);
    }
    return;
  }
  restoreValueRegion(row);
  tft_.setTextFont(2);
  tft_.setTextSize(1);
  tft_.setTextColor(TFT_WHITE);
  const int16_t x = valueX(row);
  const int16_t y = cardY(row) + 32;
  const int16_t drawn_width = tft_.drawString(value, x, y);
  if (first_dashboard_update_) {
    Serial.printf("TFT value %u: '%s' at (%d,%d), drawn_width=%d, font2=%s\n",
                  static_cast<unsigned>(row), value, x, y, drawn_width,
                  (tft_.fontsLoaded() & 0x0004) ? "loaded" : "MISSING");
  }
  strncpy(cached_values_[row], value, sizeof(cached_values_[row]) - 1);
  cached_values_[row][sizeof(cached_values_[row]) - 1] = '\0';
}

void DisplayManager::updateDashboard() {
  const uint32_t started_us = micros();
  if (first_dashboard_update_) {
    Serial.printf("TFT: first dashboard update; font mask=0x%04X, swap=%s\n",
                  static_cast<unsigned>(tft_.fontsLoaded()),
                  tft_.getSwapBytes() ? "on" : "off");
  }
  char value[32];
  drawValue(0, ESP.getChipModel());
  const uint32_t elapsed_s = millis() / 1000U;
  snprintf(value, sizeof(value), "%lud %02lu:%02lu:%02lu", static_cast<unsigned long>(elapsed_s / 86400U),
           static_cast<unsigned long>((elapsed_s / 3600U) % 24U),
           static_cast<unsigned long>((elapsed_s / 60U) % 60U),
           static_cast<unsigned long>(elapsed_s % 60U));
  drawValue(1, value);
  snprintf(value, sizeof(value), "%u KiB", ESP.getFreeHeap() / 1024U);
  drawValue(2, value);
  snprintf(value, sizeof(value), "%u MiB", ESP.getPsramSize() / 1048576U);
  drawValue(3, value);
  snprintf(value, sizeof(value), "%u KiB", ESP.getFreePsram() / 1024U);
  drawValue(4, value);
  snprintf(value, sizeof(value), "%u MiB", ESP.getFlashChipSize() / 1048576U);
  drawValue(5, value);
  snprintf(value, sizeof(value), "%dx%d", tft_.width(), tft_.height());
  drawValue(6, value);
  const uint32_t refresh_us = micros() - started_us;
  snprintf(value, sizeof(value), "%lu us", static_cast<unsigned long>(refresh_us));
  drawValue(7, value);
  Serial.printf("TFT dashboard: heap=%u B, PSRAM=%u/%u B, refresh=%lu us\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize(),
                static_cast<unsigned long>(refresh_us));
  first_dashboard_update_ = false;
}
#endif
