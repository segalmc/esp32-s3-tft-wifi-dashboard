#include "DisplayManager.h"

#include <Arduino.h>

#if TFT_HARDWARE_ENABLED
#include "ControllerLinkStatus.h"
#include "BackgroundAsset.h"
#include "BoilerPolicy.h"
#include "OriginalTelemetryApi.h"
#include "TftDashboardLayout.h"
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
    Serial.println("TFT: 24-hour history PSRAM unavailable");
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
  const uint32_t redraw_started = micros();
  drawTelemetryPage();
  last_refresh_us_ = micros() - redraw_started;
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

  // The scenic image is sent once. Opaque panels and changed fields keep
  // subsequent updates small and independent of the photo asset.
  using namespace tft_dashboard_layout;
  for (const Rect panel : {header, boiler, solar, relay, led_valve, controller, footer})
    tft_.fillRoundRect(panel.x, panel.y, panel.width, panel.height, 8, kNavy);
  tft_.drawRoundRect(boiler.x, boiler.y, boiler.width, boiler.height, 8, kTurquoise);
  tft_.drawRoundRect(solar.x, solar.y, solar.width, solar.height, 8, kTurquoise);
  tft_.setTextFont(2);
  tft_.setTextSize(1);
  tft_.setTextColor(TFT_WHITE, kNavy);
  tft_.drawString("HOT WATER", 16, 14);
  tft_.setTextColor(kTurquoise, kNavy);
  tft_.drawString("BOILER", 16, 67);
  tft_.drawString("SOLAR", 171, 67);
  tft_.drawString("BOILER RELAY", 16, 220);
  tft_.drawString("LED VALVE", 171, 220);
  tft_.drawString("ESP32 CONTROLLER", 16, 319);
  tft_.drawCircle(132, 169, 2, kTurquoise);
  tft_.drawString("C", 138, 162);
  tft_.drawCircle(287, 169, 2, kTurquoise);
  tft_.drawString("C", 293, 162);
  tft_.setTextFont(1);
  tft_.setTextColor(TFT_WHITE, kNavy);
  tft_.drawString("SIMULATED / NO MOTOR", 171, 283);
  tft_.drawString("S3 IP", 16, 399);
  tft_.drawString("LAST RX", 171, 399);
  memset(telemetry_lines_, 0, sizeof(telemetry_lines_));
  memset(telemetry_colors_, 0, sizeof(telemetry_colors_));
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
  // The temporary setup/OTA panel overlays two opaque temperature cards.
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
  history_->add(state.epoch_s, now, state.boiler_c,
                state.solar_c, !state.sensor_fault);
}

void DisplayManager::drawTelemetryPage()
{
  const uint32_t now_ms = millis();
  const auto &state = telemetry_state_;
  const uint32_t communication_age = state.ever_message ?
      now_ms - state.communication_ms : UINT32_MAX;
  original_telemetry_api::Input telemetry;
  telemetry.snapshot_ok = state.snapshot_ok;
  telemetry.ever_message = state.ever_message;
  telemetry.ever_measurement = state.ever_measurement;
  telemetry.communication_age_ms = communication_age;
  telemetry.receive_age_ms = state.ever_measurement ? now_ms - state.measurement_ms : UINT32_MAX;
  telemetry.sample_age_at_receive_ms = state.sample_age_at_receive_ms;
  telemetry.boiler_c = state.boiler_c;
  telemetry.solar_c = state.solar_c;
  telemetry.flags = state.flags;
  telemetry.valve = state.valve;
  telemetry.faults = state.faults;
  const auto sample = original_telemetry_api::view(telemetry);
  const bool boiler_valid = sample.boiler_valid;
  const bool solar_valid = sample.solar_valid;
  const bool relay_fresh = !boiler_state_.busy && boiler_state_.connected &&
      boiler_state_.relay != TuyaControl::Relay::Unknown &&
      !boiler_policy::stale(boiler_state_.last_confirmed_ms, now_ms);

  auto field = [&](unsigned index, const char *value, int x, int y,
                   int width, int height, uint16_t color,
                   uint8_t font, uint8_t size)
  {
    if (strcmp(telemetry_lines_[index], value) == 0 &&
        telemetry_colors_[index] == color) return;
    tft_.fillRect(x, y, width, height, kNavy);
    tft_.setTextFont(font);
    tft_.setTextSize(size);
    if (tft_.textWidth(value, font) > width) tft_.setTextSize(1);
    tft_.setTextColor(color, kNavy);
    tft_.drawString(value, x, y);
    strlcpy(telemetry_lines_[index], value, sizeof(telemetry_lines_[index]));
    telemetry_colors_[index] = color;
  };

  char label[64];
  field(0, network_status_, 16, 35, 220, 13,
        strcmp(network_status_, "WIFI ONLINE") == 0 ? TFT_GREEN : kOrange, 1, 1);
  time_t now = time(nullptr);
  struct tm local = {};
  char clock[8] = "--:--";
  if (now >= 1700000000 && localtime_r(&now, &local))
    snprintf(clock, sizeof(clock), "%02d:%02d", local.tm_hour, local.tm_min);
  field(1, clock, 251, 16, 55, 25, TFT_WHITE, 2, 1);

  if (!setup_panel_visible_ && !ota_panel_visible_)
  {
    if (boiler_valid) snprintf(label, sizeof(label), "%.1f", state.boiler_c);
    else strlcpy(label, "--.-", sizeof(label));
    field(2, label, 16, 102, 135, 54, kOrange, 2, 2);
    if (solar_valid) snprintf(label, sizeof(label), "%.1f", state.solar_c);
    else strlcpy(label, "--.-", sizeof(label));
    field(3, label, 171, 102, 135, 54, TFT_CYAN, 2, 2);
    const bool offline = !sample.connected;
    const bool stale = !sample.fresh;
    const char *boiler_quality = offline ? "ESP32 OFFLINE" : stale ? "STALE" :
        boiler_valid ? "LIVE DS18B20" : "SENSOR UNKNOWN";
    const char *solar_quality = offline ? "ESP32 OFFLINE" : stale ? "STALE" :
        solar_valid ? "LIVE DS18B20" : "SENSOR UNKNOWN";
    field(4, boiler_quality, 16, 184, 135, 15,
          boiler_valid ? TFT_GREEN : kOrange, 1, 1);
    field(5, solar_quality, 171, 184, 135, 15,
          solar_valid ? TFT_GREEN : kOrange, 1, 1);
  }

  const char *relay = !relay_fresh ? "UNKNOWN" :
      boiler_state_.relay == TuyaControl::Relay::On ? "ON" : "OFF";
  field(6, relay, 16, 246, 135, 33,
        !relay_fresh ? kOrange : boiler_state_.relay == TuyaControl::Relay::On ?
        TFT_GREEN : TFT_WHITE, 2, 2);
  if (boiler_state_.lease_active)
    snprintf(label, sizeof(label), "AUTO-OFF %lu:%02lu",
             static_cast<unsigned long>(boiler_state_.lease_remaining_s / 60),
             static_cast<unsigned long>(boiler_state_.lease_remaining_s % 60));
  else strlcpy(label, "AUTO-OFF IDLE", sizeof(label));
  field(7, label, 16, 285, 135, 14, TFT_WHITE, 1, 1);

  constexpr const char *valve_names[] = {"UNKNOWN", "FAULT", "OPENING",
      "CLOSING", "OPEN", "CLOSED", "FAULT"};
  const char *valve = sample.fresh && state.valve < 7 ?
      valve_names[state.valve] : "UNKNOWN";
  const uint16_t valve_color = strcmp(valve, "FAULT") == 0 ? TFT_RED :
      strcmp(valve, "OPEN") == 0 || strcmp(valve, "CLOSED") == 0 ? TFT_GREEN :
      strcmp(valve, "UNKNOWN") == 0 ? kOrange : TFT_WHITE;
  field(8, valve, 171, 246, 135, 33, valve_color, 2, 2);

  const auto link = controller_link::state(state.ever_message,
      static_cast<uint8_t>(state.transport),
      static_cast<uint8_t>(state.sender_intent),
      communication_age,
      state.ever_now ? now_ms - state.now_communication_ms : UINT32_MAX,
      state.ever_http ? now_ms - state.http_communication_ms : UINT32_MAX,
      now_ms);
  field(9, controller_link::name(link), 16, 338, 288, 32,
        link == controller_link::State::EspNow ? TFT_GREEN :
        link == controller_link::State::HttpFallback ? kOrange :
        link == controller_link::State::Offline ? TFT_RED : TFT_WHITE, 2, 2);
  if (sample.has_age)
    snprintf(label, sizeof(label), "SAMPLE %lus / %s",
             static_cast<unsigned long>(sample.age_ms / 1000),
             sample.fresh ? "LIVE" : "STALE");
  else strlcpy(label, "NO CONTROLLER MEASUREMENT", sizeof(label));
  field(10, label, 16, 371, 288, 13,
        sample.fresh ? TFT_GREEN : kOrange, 1, 1);

  const IPAddress s3_ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP() : IPAddress();
  if (WiFi.status() == WL_CONNECTED)
    snprintf(label, sizeof(label), "%u.%u.%u.%u",
             s3_ip[0], s3_ip[1], s3_ip[2], s3_ip[3]);
  else strlcpy(label, "--", sizeof(label));
  field(11, label, 16, 418, 135, 16, TFT_WHITE, 1, 1);
  const char *last_rx = !state.ever_message ? "NONE" :
      state.transport == LocalHttpTelemetry::LinkTransport::EspNow ? "ESP-NOW" :
      state.transport == LocalHttpTelemetry::LinkTransport::Http ? "HTTP" : "NONE";
  field(12, last_rx, 171, 418, 135, 17, TFT_WHITE, 2, 1);
  snprintf(label, sizeof(label), "UP %luh%02lum",
           static_cast<unsigned long>(now_ms / 3600000U),
           static_cast<unsigned long>((now_ms / 60000U) % 60U));
  field(13, label, 16, 450, 135, 14, TFT_WHITE, 1, 1);
  field(14, relay_fresh ? "TUYA VERIFIED" : "TUYA UNVERIFIED",
        171, 450, 135, 14, relay_fresh ? TFT_GREEN : kOrange, 1, 1);
}

#endif
