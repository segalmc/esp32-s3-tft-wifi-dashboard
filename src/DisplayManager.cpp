#include "DisplayManager.h"

#include <Arduino.h>

#if TFT_HARDWARE_ENABLED
#include "BackgroundAsset.h"
#include "HttpTelemetryFreshness.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <new>

#ifndef LOAD_FONT2
#error "Nature Glass values require TFT_eSPI font 2"
#endif

namespace
{
  constexpr uint32_t kDashboardIntervalMs = 1000;
  constexpr uint32_t kChartRedrawIntervalMs = 15000;
  constexpr uint16_t kNavy = 0x08C9;
  constexpr uint16_t kTurquoise = 0x4EBA;
  constexpr uint16_t kOrange = 0xFD2C;
  static_assert(sizeof(kPhotoOnlyPixels) == 320U * 480U * 2U,
                "Scenic RGB565 asset must contain exactly 320 x 480 pixels");
  uint32_t assetChecksum(const uint16_t *pixels)
  {
    uint32_t checksum = 0;
    for (size_t i = 0; i < kBackgroundWidth * kBackgroundHeight; ++i)
    {
      checksum += pgm_read_word(&pixels[i]);
    }
    return checksum;
  }
} // namespace
#endif

void DisplayManager::begin()
{
#if TFT_HARDWARE_ENABLED
  void *memory = ps_malloc(sizeof(TemperatureHistory));
  if (memory)
    history_ = new (memory) TemperatureHistory();
  else
    Serial.println("TFT: 24-hour chart PSRAM unavailable");
  Serial.printf("TFT: initializing ST7796S at 10 MHz, rotation %d\n", TFT_ROTATION);
  tft_.init();
  tft_.setRotation(TFT_ROTATION);
  Serial.printf("TFT: init commands sent; no readback (MISO disconnected). %d x %d\n",
                tft_.width(), tft_.height());
  Serial.println("TFT: permanent dashboard started");
  drawDashboardFrame();
  last_dashboard_ms_ = millis() - kDashboardIntervalMs;
#else
  Serial.println("TFT: hardware disabled; SPI and TFT GPIOs untouched");
#endif
}

void DisplayManager::update(uint32_t now_ms)
{
#if TFT_HARDWARE_ENABLED
  if (static_cast<uint32_t>(now_ms - last_dashboard_ms_) < kDashboardIntervalMs)
    return;
  last_dashboard_ms_ = now_ms;
  const time_t now = time(nullptr);
  const int64_t minute = now >= 1700000000 ? now / 60 : -1;
  if (minute != chart_minute_)
  {
    chart_minute_ = minute;
    chart_dirty_ = true;
  }
  const uint32_t redraw_started = micros();
  const bool draw_chart = chart_dirty_ &&
                          (first_chart_refresh_ || now_ms - last_chart_draw_ms_ >= kChartRedrawIntervalMs);
  drawTelemetryPage(draw_chart);
  last_refresh_us_ = micros() - redraw_started;
  if (draw_chart)
  {
    chart_dirty_ = false;
    last_chart_draw_ms_ = now_ms;
  }
#else
  (void)now_ms;
#endif
}

#if TFT_HARDWARE_ENABLED
void DisplayManager::drawDashboardFrame()
{
  const uint32_t started_us = micros();
  // The generated C array contains native RGB565 words. TFT_eSPI's ESP32-S3
  // pushImage path sends byte-swapped words by default; fillRect/drawString
  // do not use that path. Apply this to both the full image and value restores.
  tft_.setSwapBytes(true);
  background_ready_ = tft_.width() == kBackgroundWidth &&
                      tft_.height() == kBackgroundHeight;
  if (background_ready_)
  {
    background_ready_ = assetChecksum(kPhotoOnlyPixels) == kPhotoOnlyChecksum;
  }
  if (background_ready_)
  {
    // One window and one library image call. Its const overload copies one
    // flash row at a time to a small stack buffer before transmitting it.
    tft_.pushImage(0, 0, kBackgroundWidth, kBackgroundHeight,
                   kPhotoOnlyPixels);
  }
  else
  {
    Serial.println("TFT: scenic image unavailable; using solid background");
    tft_.fillScreen(kNavy);
  }

  // The scenic image stays in flash and is sent only once at startup. Small
  // opaque panels make every later update independent of the source image.
  tft_.fillRoundRect(8, 8, 304, 46, 8, kNavy);
  tft_.fillRoundRect(8, 61, 149, 87, 8, kNavy);
  tft_.fillRoundRect(163, 61, 149, 87, 8, kNavy);
  tft_.fillRoundRect(8, 154, 304, 53, 8, kNavy);
  tft_.fillRoundRect(8, 213, 149, 55, 8, kNavy);
  tft_.fillRoundRect(163, 213, 149, 55, 8, kNavy);
  tft_.fillRoundRect(8, 274, 304, 34, 8, kNavy);
  tft_.fillRoundRect(8, 314, 304, 112, 8, kNavy);
  tft_.fillRoundRect(8, 432, 304, 40, 8, kNavy);
  tft_.setTextFont(2);
  tft_.setTextSize(1);
  tft_.setTextColor(TFT_WHITE, kNavy);
  tft_.drawString("HOT WATER CONTROL", 16, 14);
  tft_.setTextColor(kTurquoise, kNavy);
  tft_.drawString("BOILER TEMP", 16, 65);
  tft_.drawString("SOLAR TEMP", 171, 65);
  tft_.drawString("BOILER SWITCH (TUYA)", 16, 158);
  tft_.drawString("VALVE", 16, 218);
  tft_.drawString("MODE", 171, 218);
  tft_.drawString("TEMPERATURE / LAST 24 HOURS", 16, 317);
  tft_.setTextFont(1);
  tft_.drawString("ESP32-S3", 14, 436);
  tft_.drawString("BOILER ESP32", 116, 436);
  tft_.drawString("TUYA / UPTIME", 225, 436);
  memset(telemetry_lines_, 0, sizeof(telemetry_lines_));
  first_chart_refresh_ = true;
  chart_dirty_ = true;
  Serial.printf("TFT: frame=%lu us, heap=%lu B, PSRAM=%lu/%lu B, photo=%s\n",
                static_cast<unsigned long>(micros() - started_us), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()), static_cast<unsigned long>(ESP.getPsramSize()),
                background_ready_ ? "yes" : "fallback");
  dashboard_ready_ = true;
}

void DisplayManager::drawSetupPanel(bool visible)
{
  if (!visible)
  {
    setup_panel_visible_ = false;
    drawDashboardFrame();
    return;
  }
  restoreTopPanelRegion();
  if (visible)
  {
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
}

void DisplayManager::restoreTopPanelRegion()
{
  // This is the same even-width photo region used by the temporary AP panel.
  if (background_ready_)
    streamAssetRegion(kPhotoOnlyPixels, 20, 90, 280, 60);
  else
    tft_.fillRect(20, 90, 280, 60, kNavy);
}

void DisplayManager::updateClock(bool, uint32_t) {}

void DisplayManager::updateNetwork(const char *status, bool ap_active,
                                   bool initial_setup, const char *ap_password,
                                   bool reveal, uint32_t now_ms)
{
  if (strncmp(network_status_, status, sizeof(network_status_)) != 0)
  {
    strncpy(network_status_, status, sizeof(network_status_) - 1);
    network_status_[sizeof(network_status_) - 1] = '\0';
  }
  if (!dashboard_ready_)
    return;
  if (ap_active && ap_password && ap_password[0])
  {
    memcpy(setup_password_, ap_password, sizeof(setup_password_));
    if (initial_setup && !initial_password_shown_)
    {
      reveal_until_ms_ = now_ms + 60000U;
      initial_password_shown_ = true;
    }
    if (reveal)
      reveal_until_ms_ = now_ms + 60000U;
  }
  const bool visible = ap_active && reveal_until_ms_ &&
                       static_cast<int32_t>(now_ms - reveal_until_ms_) < 0;
  if (visible != setup_panel_visible_)
  {
    if (ota_panel_visible_)
    {
      setup_panel_visible_ = visible;
    }
    else
    {
      drawSetupPanel(visible);
    }
  }
  if (!ap_active)
  {
    reveal_until_ms_ = 0;
    memset(setup_password_, 0, sizeof(setup_password_));
  }
}

void DisplayManager::drawOtaPanel(NatureNetworkManager::OtaPhase phase, uint8_t percent,
                                  uint8_t error, bool show_password)
{
  restoreTopPanelRegion();
  tft_.fillRoundRect(22, 91, 276, 58, 10, kNavy);
  tft_.drawRoundRect(22, 91, 276, 58, 10, kTurquoise);
  tft_.setTextFont(1);
  tft_.setTextSize(1);
  tft_.setTextColor(kTurquoise);
  if (show_password)
  {
    tft_.drawString("OTA PASSWORD / USB ONLY", 31, 99);
    tft_.setTextFont(2);
    if (tft_.textWidth(ota_password_, 2) > 250)
      tft_.setTextFont(1);
    tft_.setTextColor(TFT_WHITE);
    tft_.drawString(ota_password_, 31, 114);
    tft_.setTextFont(1);
    tft_.setTextColor(kOrange);
    tft_.drawString("HIDES IN 60 SECONDS", 31, 138);
  }
  else if (phase == NatureNetworkManager::OtaPhase::Error)
  {
    constexpr const char *errors[] = {"AUTH FAILED", "START FAILED", "CONNECT FAILED",
                                      "RECEIVE FAILED", "IMAGE FAILED"};
    tft_.drawString("OTA UPDATE ERROR", 31, 99);
    tft_.setTextFont(2);
    tft_.setTextColor(kOrange);
    tft_.drawString(error < 5 ? errors[error] : "UNKNOWN ERROR", 31, 117);
  }
  else if (phase == NatureNetworkManager::OtaPhase::Success)
  {
    tft_.drawString("OTA UPDATE", 31, 99);
    tft_.setTextFont(2);
    tft_.setTextColor(TFT_WHITE);
    tft_.drawString("COMPLETE / REBOOTING", 31, 117);
  }
  else
  {
    char label[24];
    snprintf(label, sizeof(label), "OTA UPDATE / %u%%", static_cast<unsigned>(percent));
    tft_.drawString(label, 31, 99);
    tft_.drawRoundRect(31, 123, 258, 14, 4, kTurquoise);
    tft_.fillRect(34, 126, percent * 252 / 100, 8, kTurquoise);
  }
}

void DisplayManager::updateOta(NatureNetworkManager::OtaSnapshot ota, const char *password,
                               bool reveal, uint32_t now_ms)
{
  if (reveal && password && password[0])
  {
    memcpy(ota_password_, password, sizeof(ota_password_));
    ota_reveal_until_ms_ = now_ms + 60000U;
  }
  if (ota.phase == NatureNetworkManager::OtaPhase::Error &&
      ota.event != ota_last_error_event_)
  {
    ota_error_until_ms_ = now_ms + 10000U;
    ota_last_error_event_ = ota.event;
  }
  if (!dashboard_ready_)
    return;

  const bool show_error = ota.phase == NatureNetworkManager::OtaPhase::Error &&
                          static_cast<int32_t>(now_ms - ota_error_until_ms_) < 0;
  const bool show_progress = ota.phase == NatureNetworkManager::OtaPhase::Uploading ||
                             ota.phase == NatureNetworkManager::OtaPhase::Success;
  const bool show_password = !show_error && !show_progress &&
                             ota_reveal_until_ms_ &&
                             static_cast<int32_t>(now_ms - ota_reveal_until_ms_) < 0;
  if (show_error || show_progress)
  {
    memset(ota_password_, 0, sizeof(ota_password_));
    ota_reveal_until_ms_ = 0;
  }
  const bool visible = show_error || show_progress || show_password;
  if (!visible)
  {
    if (ota_panel_visible_)
    {
      ota_panel_visible_ = false;
      memset(ota_password_, 0, sizeof(ota_password_));
      ota_reveal_until_ms_ = 0;
      if (setup_panel_visible_)
        drawSetupPanel(true);
      else
        drawDashboardFrame();
    }
    return;
  }

  const uint8_t stepped_percent = (ota.percent / 5) * 5;
  if (!ota_panel_visible_ || ota_secret_visible_ != show_password ||
      ota_drawn_phase_ != ota.phase || ota_drawn_error_ != ota.error ||
      (!show_password && show_progress && stepped_percent != ota_drawn_percent_))
  {
    drawOtaPanel(ota.phase, stepped_percent, ota.error, show_password);
    ota_panel_visible_ = true;
    ota_secret_visible_ = show_password;
    ota_drawn_phase_ = ota.phase;
    ota_drawn_percent_ = stepped_percent;
    ota_drawn_error_ = ota.error;
  }
}

void DisplayManager::streamAssetRegion(const uint16_t *pixels, int16_t x,
                                       int16_t y, int16_t width,
                                       int16_t height)
{
  // The asset stride is always 320, even for a narrow card region. A single
  // address window keeps adjacent scan lines contiguous on the display.
  alignas(4) uint16_t row[kBackgroundWidth];
  tft_.startWrite();
  tft_.setAddrWindow(x, y, width, height);
  for (int16_t line = 0; line < height; ++line)
  {
    const size_t source = static_cast<size_t>(y + line) * kBackgroundWidth + x;
    for (int16_t column = 0; column < width; ++column)
    {
      row[column] = pgm_read_word(&pixels[source + column]);
    }
    tft_.pushPixels(row, width);
  }
  tft_.endWrite();
}

void DisplayManager::updateBoiler(TuyaControl::Snapshot state)
{
  boiler_state_ = state;
}

void DisplayManager::updateTelemetry(LocalHttpTelemetry::Snapshot state)
{
  if (!state.snapshot_ok)
    return;
  telemetry_state_ = state;
  const time_t now = time(nullptr);
  if (history_ && now != last_history_expire_second_)
  {
    history_->expire(now);
    last_history_expire_second_ = now;
  }
  if (!history_ || !state.ever_measurement || !state.epoch_s)
    return;
  if (last_telemetry_boot_ == state.boot_nonce &&
      last_telemetry_sequence_ == state.sequence)
    return;
  last_telemetry_boot_ = state.boot_nonce;
  last_telemetry_sequence_ = state.sequence;
  if (history_->add(state.epoch_s, now, state.boiler_c,
                    state.solar_c, !state.sensor_fault))
    chart_dirty_ = true;
}

void DisplayManager::drawTelemetryPage(bool chart)
{
  const auto &state = telemetry_state_;
  const auto comm = http_telemetry::communication(
      state.ever_message, millis() - state.communication_ms);
  const auto measurement = http_telemetry::measurement(
      state.ever_measurement, millis() - state.measurement_ms);
  const bool stale = measurement == http_telemetry::Measurement::stale ||
                     measurement == http_telemetry::Measurement::unknown;
  char label[64];
  auto line = [&](unsigned index, const char *value, int x, int y, int width,
                  int height, uint16_t color, uint8_t font)
  {
    if (strcmp(telemetry_lines_[index], value) == 0)
      return;
    tft_.fillRect(x, y, width, height, kNavy);
    tft_.setTextFont(font == 4 ? 2 : font);
    tft_.setTextSize(font == 4 ? 2 : 1);
    tft_.setTextColor(color, kNavy);
    tft_.drawString(value, x, y);
    strlcpy(telemetry_lines_[index], value, sizeof(telemetry_lines_[index]));
  };
  time_t now = time(nullptr);
  struct tm local = {};
  char clock[8] = "--:--";
  if (now >= 1700000000 && localtime_r(&now, &local))
    snprintf(clock, sizeof(clock), "%02d:%02d", local.tm_hour, local.tm_min);
  snprintf(label, sizeof(label), "%s  HTTP %s  %s", network_status_,
           http_telemetry::name(comm), clock);
  line(0, label, 16, 35, 288, 16,
       comm == http_telemetry::Communication::live ? TFT_GREEN : kOrange, 2);
  if (!setup_panel_visible_ && !ota_panel_visible_)
  {
    if (!stale && isfinite(state.boiler_c))
      snprintf(label, sizeof(label), "%.1f C", state.boiler_c);
    else
      strlcpy(label, "--.- C", sizeof(label));
    line(1, label, 16, 92, 135, 32, kOrange, 4);
    if (!stale && isfinite(state.solar_c))
      snprintf(label, sizeof(label), "%.1f C", state.solar_c);
    else
      strlcpy(label, "--.- C", sizeof(label));
    line(2, label, 171, 92, 135, 32, TFT_CYAN, 4);
  }
  const char *relay = boiler_state_.relay == TuyaControl::Relay::On ? "ON" : boiler_state_.relay == TuyaControl::Relay::Off ? "OFF"
                                                                                                                            : "UNKNOWN";
  snprintf(label, sizeof(label), "%s / %s", relay,
           boiler_state_.connected ? "ONLINE" : "OFFLINE");
  line(3, label, 16, 180, 288, 18, boiler_state_.connected ? TFT_GREEN : kOrange, 2);
  if (boiler_state_.lease_active)
    snprintf(label, sizeof(label), "Auto off in %lu:%02lu",
             static_cast<unsigned long>(boiler_state_.lease_remaining_s / 60),
             static_cast<unsigned long>(boiler_state_.lease_remaining_s % 60));
  else
    strlcpy(label, "Auto off: idle", sizeof(label));
  line(4, label, 16, 198, 288, 10, TFT_WHITE, 1);
  const char* valve_names[] = {"UNKNOWN", "INTERLOCK", "OPENING", "CLOSING",
                               "OPEN ASSUMED", "CLOSED ASSUMED", "FAULT"};
  line(5, stale || state.valve > 6 ? "UNKNOWN" : valve_names[state.valve], 16, 240, 135, 22,
       stale ? kOrange : TFT_GREEN, 2);
  line(6, state.sensor_fault ? "SENSOR FAULT" : stale ? "UNKNOWN" : "AUTO",
       171, 240,
       135, 22, TFT_WHITE, 2);
  const uint32_t comm_age = state.ever_message ?
      (millis() - state.communication_ms) / 1000 : 0;
  const uint32_t measurement_age = state.ever_measurement ?
      (millis() - state.measurement_ms) / 1000 : 0;
  snprintf(label, sizeof(label), "C:%s %lus M:%s %lus%s",
           http_telemetry::name(comm), static_cast<unsigned long>(comm_age),
           http_telemetry::name(measurement),
           static_cast<unsigned long>(measurement_age),
           comm == http_telemetry::Communication::live && stale ? " BACKLOG" : "");

  line(7, label, 16, 283, 288, 18,
       stale ? kOrange : TFT_GREEN, 2);
  const IPAddress esp_ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
  snprintf(label, sizeof(label), "%u.%u.%u.%u", esp_ip[0], esp_ip[1], esp_ip[2], esp_ip[3]);
  line(8, label, 14, 451, 100, 12, TFT_WHITE, 1);
  line(9, state.peer_ip[0] ? state.peer_ip : "--", 116, 451, 106, 12, TFT_WHITE, 1);
  snprintf(label, sizeof(label), "%s %luh%02lum", boiler_state_.connected ? "ON" : "OFF",
           static_cast<unsigned long>(millis() / 3600000U),
           static_cast<unsigned long>((millis() / 60000U) % 60U));
  line(10, label, 225, 451, 81, 12, boiler_state_.connected ? TFT_GREEN : kOrange, 1);
  first_chart_refresh_ = false;
  if (!chart)
    return;
  tft_.fillRect(14, 335, 292, 88, kNavy);
  tft_.setTextFont(1);
  tft_.setTextSize(1);
  tft_.setTextColor(TFT_WHITE, kNavy);
  if (!history_ || chart_minute_ < 0)
  {
    tft_.drawString("Waiting for trusted time and samples", 20, 365);
    return;
  }
  constexpr int x0 = 38, width = 262, top = 344, height = 60;
  float low = 150, high = -50;
  const int64_t first = (chart_minute_ * 60 - 86400 + 59) / 60;
  for (int64_t minute = first; minute <= chart_minute_; ++minute)
  {
    auto b = history_->at(minute);
    if (b.minute < 0)
      continue;
    low = min(low, min(b.boiler_min, b.solar_min));
    high = max(high, max(b.boiler_max, b.solar_max));
  }
  if (high < low)
  {
    tft_.drawString("No fresh samples", 18, 325);
    return;
  }
  low = floorf((low - 2) / 10) * 10;
  high = ceilf((high + 2) / 10) * 10;
  if (high - low < 10)
    high = low + 10;
  tft_.drawRect(x0, top, width, height, 0x8410);
  char axis[20];
  snprintf(axis, sizeof(axis), "%.0f C", high);
  tft_.drawString(axis, 2, top);
  snprintf(axis, sizeof(axis), "%.0f C", low);
  tft_.drawString(axis, 2, top + height - 12);
  auto y = [&](float value)
  { return top + height - 2 - static_cast<int>((value - low) * (height - 4) / (high - low)); };
  int previous_boiler = -1, previous_solar = -1, previous_x = -1;
  int64_t previous_last_minute = -1;
  auto plot_run = [&](int x, int64_t run_first, int64_t run_last,
                      float bmin, float bmax, float smin, float smax)
  {
    const int px = x0 + x;
    tft_.drawLine(px, y(bmin), px, y(bmax), TFT_ORANGE);
    tft_.drawLine(px, y(smin), px, y(smax), TFT_CYAN);
    const int boiler_y = y((bmin + bmax) / 2), solar_y = y((smin + smax) / 2);
    if (previous_last_minute >= 0 && run_first == previous_last_minute + 1)
    {
      tft_.drawLine(previous_x, previous_boiler, px, boiler_y, TFT_ORANGE);
      tft_.drawLine(previous_x, previous_solar, px, solar_y, TFT_CYAN);
    }
    previous_x = px;
    previous_boiler = boiler_y;
    previous_solar = solar_y;
    previous_last_minute = run_last;
  };
  for (int x = 0; x < width; ++x)
  {
    const int64_t begin = first + static_cast<int64_t>(x) * 1440 / width;
    const int64_t end = first + static_cast<int64_t>(x + 1) * 1440 / width;
    float bmin = 151, bmax = -51, smin = 151, smax = -51;
    int64_t run_first = -1, run_last = -1;
    for (int64_t minute = begin; minute < end && minute <= chart_minute_; ++minute)
    {
      const auto b = history_->at(minute);
      if (b.minute < 0)
      {
        if (run_first >= 0)
          plot_run(x, run_first, run_last, bmin, bmax, smin, smax);
        run_first = -1;
        previous_last_minute = -1;
        bmin = smin = 151;
        bmax = smax = -51;
        continue;
      }
      if (run_first < 0)
        run_first = minute;
      run_last = minute;
      bmin = min(bmin, b.boiler_min);
      bmax = max(bmax, b.boiler_max);
      smin = min(smin, b.solar_min);
      smax = max(smax, b.solar_max);
    }
    if (run_first >= 0)
      plot_run(x, run_first, run_last, bmin, bmax, smin, smax);
  }
  time_t left_time = first * 60, right_time = chart_minute_ * 60;
  localtime_r(&left_time, &local);
  strftime(axis, sizeof(axis), "%H:%M", &local);
  tft_.drawString(axis, x0, top + height + 4);
  localtime_r(&right_time, &local);
  strftime(axis, sizeof(axis), "%H:%M", &local);
  tft_.drawString(axis, x0 + width - 35, top + height + 4);
  tft_.drawString("-24h", x0 + 39, top + height + 4);
  tft_.drawString("now", x0 + width - 65, top + height + 4);
  tft_.setTextColor(TFT_ORANGE, kNavy);
  tft_.drawString("Boiler", 132, 335);
  tft_.setTextColor(TFT_CYAN, kNavy);
  tft_.drawString("Solar", 184, 335);
}

#endif
