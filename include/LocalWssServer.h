#pragma once

#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <ArduinoJson.h>
#include <atomic>
#include <WiFi.h>
#include "NetworkManager.h"
#ifdef HTTP_ANY
#undef HTTP_ANY  // Arduino WebServer and ESP-IDF HTTPD define different forms.
#endif
#include <esp_https_server.h>
#include <freertos/semphr.h>

class LocalWssServer {
 public:
  struct Telemetry {
    bool snapshot_ok = false;
    bool valid = false;
    bool connected = false;
    bool stale = true;
    float boiler_c = NAN;
    float solar_c = NAN;
    bool manual = false;
    char quality[9] = "unknown";
    char valve[8] = "unknown";
    char boot_id[129] = {};
    char device_session_id[37] = {};
    char observed_at[32] = {};
    char peer_ip[16] = {};
    uint32_t sequence = 0;
    uint32_t received_ms = 0;
    int64_t observed_epoch = 0;
  };
  void begin(NatureNetworkManager* network);
  Telemetry telemetry() const;
  void diagnostics() const;

 private:
  enum class Phase : uint8_t { Empty, Challenge, Ready };
  static void taskEntry(void* context);
  static esp_err_t wsEntry(httpd_req_t* req);
  static void pingWork(void* context);
  static void tlsError(void* context, esp_event_base_t, int32_t, void*);
  void run();
  esp_err_t onFrame(httpd_req_t* req);
  void clearSession();
  bool allowed(IPAddress* ip = nullptr) const;
  bool configured() const;
  void closeClient(const char* reason);
  void reject(httpd_req_t* req);
  void onHello(httpd_req_t* req, JsonObjectConst frame);
  void onResponse(httpd_req_t* req, JsonObjectConst frame);
  void onState(httpd_req_t* req, JsonObjectConst frame);
  bool sendJson(httpd_req_t* req, const char* data);

  NatureNetworkManager* network_ = nullptr;
  std::atomic<bool> clock_integrity_{true};
  std::atomic<httpd_handle_t> server_{nullptr};
  IPAddress sta_ip_;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  Telemetry telemetry_{};
  int client_fd_ = -1;
  Phase phase_ = Phase::Empty;
  uint32_t phase_ms_ = 0;
  uint32_t last_frame_ms_ = 0;
  uint32_t last_ping_ms_ = 0;
  bool awaiting_pong_ = false;
  char boot_id_[129] = {};
  char device_session_id_[37] = {};
  char connection_session_id_[37] = {};
  char last_device_session_id_[37] = {};
  char last_connection_session_id_[37] = {};
  char server_nonce_[65] = {};
  char expires_at_[32] = {};
  uint32_t auth_failures_ = 0;
  std::atomic<uint32_t> tls_failures_{0};
  uint32_t connection_resets_ = 0;
  uint32_t accepted_ = 0;
  uint32_t rejected_ = 0;
  uint32_t rejected_telemetry_ = 0;
  uint32_t http_stack_free_bytes_ = 0;
  uint32_t mutex_misses_ = 0;
  std::atomic<uint32_t> task_lag_max_ms_{0};
  std::atomic<uint32_t> ping_send_errors_{0};
  mutable std::atomic<uint32_t> snapshot_misses_{0};
  TaskHandle_t task_ = nullptr;
};
#endif
