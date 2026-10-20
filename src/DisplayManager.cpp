#include "DisplayManager.h"

#include <Arduino.h>

#if TFT_HARDWARE_ENABLED
#include "BackgroundAsset.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

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
  dashboard_ready_ = true;
}

void DisplayManager::drawNetworkStatus() {
  // Restore only this even-width header region from the proven RGB565 path.
  if (background_ready_) streamAssetRegion(kBackgroundPixels, 194, 47, 110, 22);
  else tft_.fillRect(194, 47, 110, 22, 0x11E8);
  tft_.fillRoundRect(196, 48, 106, 19, 6, kNavy);
  tft_.setTextFont(1);
  tft_.setTextSize(1);
  tft_.setTextColor(kTurquoise);
  tft_.drawString(network_status_, 202, 54);
  network_status_dirty_ = false;
}

void DisplayManager::drawSetupPanel(bool visible) {
  restoreTopPanelRegion();
  if (visible) {
    tft_.fillRoundRect(22, 91, 276, 58, 10, kNavy);
    tft_.drawRoundRect(22, 91, 276, 58, 10, kTurquoise);
    tft_.setTextFont(1);
    tft_.setTextSize(1);
    tft_.setTextColor(kTurquoise);
    tft_.drawString("SETUP AP  /  192.168.4.1", 31, 99);
    tft_.setTextFont(2);
    tft_.setTextColor(TFT_WHITE);
    tft_.drawString(setup_password_, 31, 116);
    tft_.setTextFont(1);
    tft_.setTextColor(kOrange);
    tft_.drawString("RECORD THIS PASSWORD", 31, 138);
  }
  setup_panel_visible_ = visible;
  clock_visible_ = false;
  clock_dirty_ = true;
}

void DisplayManager::restoreTopPanelRegion() {
  // This is the same even-width photo region used by the temporary AP panel.
  if (background_ready_) streamAssetRegion(kBackgroundPixels, 20, 90, 280, 60);
  else tft_.fillRect(20, 90, 280, 60, kNavy);
}

void DisplayManager::drawClockPanel(const char* time_text, const char* weekday,
                                    const char* date_text) {
  restoreTopPanelRegion();
  tft_.fillRoundRect(22, 91, 276, 58, 10, kNavy);
  tft_.drawRoundRect(22, 91, 276, 58, 10, kTurquoise);
  tft_.setTextFont(2);
  tft_.setTextSize(2);
  tft_.setTextColor(TFT_WHITE);
  tft_.drawString(time_text, 31, 94);
  tft_.setTextSize(1);
  tft_.setTextColor(kTurquoise);
  tft_.drawString(weekday, 31, 129);
  const int16_t separator_x = 31 + tft_.textWidth(weekday, 2) + 9;
  tft_.fillCircle(separator_x, 137, 2, kOrange);
  tft_.setTextColor(TFT_WHITE);
  tft_.drawString(date_text, separator_x + 9, 129);
  clock_visible_ = true;
  clock_dirty_ = false;
}

void DisplayManager::updateClock(bool ntp_synced, uint32_t now_ms) {
  if (ntp_synced) clock_was_synced_ = true;
  if (!dashboard_ready_ || !clock_was_synced_ || setup_panel_visible_ ||
      ota_panel_visible_) return;
  // Sample once a second; HH:mm only needs a redraw when the minute or date changes.
  if (!clock_dirty_ && static_cast<uint32_t>(now_ms - last_clock_sample_ms_) < 1000U) return;
  last_clock_sample_ms_ = now_ms;
  const time_t epoch = time(nullptr);
  struct tm local = {};
  if (!localtime_r(&epoch, &local) || local.tm_year < 124 || local.tm_year >= 200 ||
      local.tm_wday < 0 || local.tm_wday > 6) {
    if (clock_visible_ || clock_dirty_) restoreTopPanelRegion();
    clock_visible_ = false;
    clock_dirty_ = false;
    cached_clock_time_[0] = '\0';
    return;
  }
  constexpr const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                       "Thursday", "Friday", "Saturday"};
  char time_text[6];
  char date_text[16];
  snprintf(time_text, sizeof(time_text), "%02d:%02d", local.tm_hour, local.tm_min);
  snprintf(date_text, sizeof(date_text), "%02d/%02d/%04d", local.tm_mday,
           local.tm_mon + 1, local.tm_year + 1900);
  if (clock_dirty_ || strcmp(cached_clock_time_, time_text) != 0 ||
      strcmp(cached_clock_date_, date_text) != 0 || cached_weekday_ != local.tm_wday) {
    drawClockPanel(time_text, kWeekdays[local.tm_wday], date_text);
    memcpy(cached_clock_time_, time_text, sizeof(time_text));
    strncpy(cached_clock_date_, date_text, sizeof(cached_clock_date_) - 1);
    cached_clock_date_[sizeof(cached_clock_date_) - 1] = '\0';
    cached_weekday_ = static_cast<uint8_t>(local.tm_wday);
  }
}

void DisplayManager::updateNetwork(const char* status, bool ap_active,
                                   bool initial_setup, const char* ap_password,
                                   bool reveal, uint32_t now_ms) {
  if (strncmp(network_status_, status, sizeof(network_status_)) != 0) {
    strncpy(network_status_, status, sizeof(network_status_) - 1);
    network_status_[sizeof(network_status_) - 1] = '\0';
    network_status_dirty_ = true;
  }
  if (!dashboard_ready_) return;
  if (network_status_dirty_) drawNetworkStatus();
  if (ap_active && ap_password && ap_password[0]) {
    memcpy(setup_password_, ap_password, sizeof(setup_password_));
    if (initial_setup && !initial_password_shown_) {
      reveal_until_ms_ = now_ms + 60000U;
      initial_password_shown_ = true;
    }
    if (reveal) reveal_until_ms_ = now_ms + 60000U;
  }
  const bool visible = ap_active && reveal_until_ms_ &&
                       static_cast<int32_t>(now_ms - reveal_until_ms_) < 0;
  if (visible != setup_panel_visible_) {
    if (ota_panel_visible_) {
      setup_panel_visible_ = visible;
      clock_dirty_ = true;
    } else if (!visible && clock_was_synced_) {
      // updateClock runs next in this loop and replaces the password directly.
      setup_panel_visible_ = false;
      clock_visible_ = false;
      clock_dirty_ = true;
    } else {
      drawSetupPanel(visible);
    }
  }
  if (!ap_active) {
    reveal_until_ms_ = 0;
    memset(setup_password_, 0, sizeof(setup_password_));
  }
}

void DisplayManager::drawOtaPanel(NetworkManager::OtaPhase phase, uint8_t percent,
                                  uint8_t error, bool show_password) {
  restoreTopPanelRegion();
  tft_.fillRoundRect(22, 91, 276, 58, 10, kNavy);
  tft_.drawRoundRect(22, 91, 276, 58, 10, kTurquoise);
  tft_.setTextFont(1);
  tft_.setTextSize(1);
  tft_.setTextColor(kTurquoise);
  if (show_password) {
    tft_.drawString("OTA PASSWORD / USB ONLY", 31, 99);
    tft_.setTextFont(2);
    if (tft_.textWidth(ota_password_, 2) > 250) tft_.setTextFont(1);
    tft_.setTextColor(TFT_WHITE);
    tft_.drawString(ota_password_, 31, 114);
    tft_.setTextFont(1);
    tft_.setTextColor(kOrange);
    tft_.drawString("HIDES IN 60 SECONDS", 31, 138);
  } else if (phase == NetworkManager::OtaPhase::Error) {
    constexpr const char* errors[] = {"AUTH FAILED", "START FAILED", "CONNECT FAILED",
                                      "RECEIVE FAILED", "IMAGE FAILED"};
    tft_.drawString("OTA UPDATE ERROR", 31, 99);
    tft_.setTextFont(2);
    tft_.setTextColor(kOrange);
    tft_.drawString(error < 5 ? errors[error] : "UNKNOWN ERROR", 31, 117);
  } else if (phase == NetworkManager::OtaPhase::Success) {
    tft_.drawString("OTA UPDATE", 31, 99);
    tft_.setTextFont(2);
    tft_.setTextColor(TFT_WHITE);
    tft_.drawString("COMPLETE / REBOOTING", 31, 117);
  } else {
    char label[24];
    snprintf(label, sizeof(label), "OTA UPDATE / %u%%", static_cast<unsigned>(percent));
    tft_.drawString(label, 31, 99);
    tft_.drawRoundRect(31, 123, 258, 14, 4, kTurquoise);
    tft_.fillRect(34, 126, percent * 252 / 100, 8, kTurquoise);
  }
  clock_visible_ = false;
  clock_dirty_ = true;
}

void DisplayManager::updateOta(NetworkManager::OtaSnapshot ota, const char* password,
                               bool reveal, uint32_t now_ms) {
  if (reveal && password && password[0]) {
    memcpy(ota_password_, password, sizeof(ota_password_));
    ota_reveal_until_ms_ = now_ms + 60000U;
  }
  if (ota.phase == NetworkManager::OtaPhase::Error &&
      ota.event != ota_last_error_event_) {
    ota_error_until_ms_ = now_ms + 10000U;
    ota_last_error_event_ = ota.event;
  }
  if (!dashboard_ready_) return;

  const bool show_error = ota.phase == NetworkManager::OtaPhase::Error &&
                          static_cast<int32_t>(now_ms - ota_error_until_ms_) < 0;
  const bool show_progress = ota.phase == NetworkManager::OtaPhase::Uploading ||
                             ota.phase == NetworkManager::OtaPhase::Success;
  const bool show_password = !show_error && !show_progress &&
                             ota_reveal_until_ms_ &&
                             static_cast<int32_t>(now_ms - ota_reveal_until_ms_) < 0;
  if (show_error || show_progress) {
    memset(ota_password_, 0, sizeof(ota_password_));
    ota_reveal_until_ms_ = 0;
  }
  const bool visible = show_error || show_progress || show_password;
  if (!visible) {
    if (ota_panel_visible_) {
      ota_panel_visible_ = false;
      memset(ota_password_, 0, sizeof(ota_password_));
      ota_reveal_until_ms_ = 0;
      if (setup_panel_visible_) drawSetupPanel(true);
      else restoreTopPanelRegion();
      clock_dirty_ = true;
    }
    return;
  }

  const uint8_t stepped_percent = (ota.percent / 5) * 5;
  if (!ota_panel_visible_ || ota_secret_visible_ != show_password ||
      ota_drawn_phase_ != ota.phase || ota_drawn_error_ != ota.error ||
      (!show_password && show_progress && stepped_percent != ota_drawn_percent_)) {
    drawOtaPanel(ota.phase, stepped_percent, ota.error, show_password);
    ota_panel_visible_ = true;
    ota_secret_visible_ = show_password;
    ota_drawn_phase_ = ota.phase;
    ota_drawn_percent_ = stepped_percent;
    ota_drawn_error_ = ota.error;
  }
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
  last_refresh_us_ = refresh_us;
  snprintf(value, sizeof(value), "%lu us", static_cast<unsigned long>(refresh_us));
  drawValue(7, value);
  Serial.printf("TFT dashboard: heap=%u B, PSRAM=%u/%u B, refresh=%lu us\n",
                ESP.getFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize(),
                static_cast<unsigned long>(refresh_us));
  first_dashboard_update_ = false;
}
#endif
