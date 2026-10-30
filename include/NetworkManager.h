#pragma once

#include "TftConfig.h"

#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
class TuyaControl;

class NatureNetworkManager {
 public:
  enum class State : uint8_t { Setup, Connecting, Online, RetryWait, Error };
  struct Snapshot {
    State state;
    bool ap_active;
    bool clock_synced;
    bool initial_setup;
    uint32_t clock_sync_generation;
  };
  enum class OtaPhase : uint8_t { Idle, Uploading, Success, Error };
  struct OtaSnapshot {
    OtaPhase phase;
    uint8_t percent;
    uint8_t error;
    uint32_t event;
  };

  void begin();
  void attachTuya(TuyaControl* control) { tuya_ = control; }
  void update();  // USB commands only; Wi-Fi and HTTP run in the network task.
  Snapshot snapshot() const;
  bool copySetupPassword(char* output, size_t size) const;
  bool takeRevealRequest();
  OtaSnapshot otaSnapshot() const;
  bool copyOtaPassword(char* output, size_t size) const;
  bool takeOtaRevealRequest();

 private:
  struct Credentials {
    uint32_t magic;
    char ssid[33];
    char password[65];
  };
  enum class Command : uint8_t { Setup, RotateOta };
  static void taskEntry(void* context);
  void run();
  void startConnection(uint32_t now);
  void startPortal();
  void stopPortal();
  void scheduleRetry(uint32_t now);
  void setState(State state);
  bool loadStorage();
  void processCommands();
  void processSerial();
  void tick(uint32_t now);
  void configurePortal();
  void handleRoot();
  void handleScan();
  void handleSave();
  void handleStatus();
  bool apRequest();
  bool validCredentials(const Credentials& value) const;
  void startClock();
  void checkClock(uint32_t now);
  void clearPending();
  void serviceOta();
  void stopOta();
  void setOtaStatus(OtaPhase phase, uint8_t percent, uint8_t error);
  void rotateOtaPassword();

  mutable portMUX_TYPE snapshot_lock_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_{State::Error, false, false, false, 0};
  OtaSnapshot ota_snapshot_{OtaPhase::Idle, 0, 0, 0};
  char ap_password_[17] = {};
  char ota_password_[21] = {};
  bool ota_password_ready_ = false;
  bool ota_started_ = false;
  bool ota_reveal_requested_ = false;
  Credentials saved_{};
  bool has_saved_ = false;
  Credentials pending_{};
  bool has_pending_ = false;
  bool portal_manual_ = false;
  bool portal_configured_ = false;
  bool portal_closing_ = false;
  bool clock_warning_sent_ = false;
  uint32_t portal_started_ms_ = 0;
  uint32_t portal_close_at_ms_ = 0;
  uint32_t clock_started_ms_ = 0;
  uint32_t scan_started_ms_ = 0;
  uint32_t portal_stats_started_ms_ = 0;
  uint32_t portal_max_us_ = 0;
  uint32_t portal_slow_calls_ = 0;
  bool scan_running_ = false;
  String scan_json_ = "{\"scanning\":true}";
  char csrf_token_[33] = {};
  const char* portal_result_ = "ready";
  bool reveal_requested_ = false;
  char serial_line_[32] = {};
  uint8_t serial_length_ = 0;
  QueueHandle_t commands_ = nullptr;
  TuyaControl* tuya_ = nullptr;
  Preferences preferences_;
  DNSServer dns_;
  WebServer server_{IPAddress(192, 168, 4, 1), 80};
  uint32_t offline_since_ms_ = 0;
  uint32_t attempt_started_ms_ = 0;
  uint32_t retry_at_ms_ = 0;
  uint32_t next_ap_attempt_ms_ = 0;
  uint32_t retry_delay_ms_ = 1000;
};
#endif
