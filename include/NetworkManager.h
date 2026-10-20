#pragma once

#include "TftConfig.h"

#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

class NetworkManager {
 public:
  enum class State : uint8_t { Setup, Connecting, Online, RetryWait, Error };
  struct Snapshot {
    State state;
    bool ap_active;
    bool clock_synced;
    bool initial_setup;
  };

  void begin();
  void update();  // USB commands only; Wi-Fi and HTTP run in the network task.
  Snapshot snapshot() const;
  bool copySetupPassword(char* output, size_t size) const;
  bool takeRevealRequest();

 private:
  struct Credentials {
    uint32_t magic;
    char ssid[33];
    char password[65];
  };
  enum class Command : uint8_t { Setup };
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

  mutable portMUX_TYPE snapshot_lock_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_{State::Error, false, false, false};
  char ap_password_[17] = {};
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
