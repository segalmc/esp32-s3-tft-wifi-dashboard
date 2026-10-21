#pragma once
#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <Preferences.h>
#include "NetworkManager.h"
#include "LynkTuya.h"

class TuyaControl {
 public:
  enum class Relay : uint8_t { Unknown, Off, On };
  enum class Progress : uint8_t { Idle, Pending, Sending, Verifying, Confirmed, Uncertain, Failed };
  enum class Request : uint8_t { On, Off, Status, Reconnect, ArmMonitor, DisarmMonitor };
  enum class Accept : uint8_t { Accepted, Busy, Disabled, Offline, Clock, Storage, LeaseActive, StateUnknown };
  struct Snapshot {
    Relay relay;
    Progress progress;
    bool connected;
    bool reads_enabled;
    bool control_enabled;
    bool lease_active;
    bool monitor_armed;
    bool storage_ready;
    bool busy;
    uint32_t last_confirmed_ms;
    uint32_t failures;
    uint32_t connection_failures;
    uint32_t authentication_failures;
    uint32_t auth_or_parse_failures;
    uint32_t command_id;
    uint32_t push_events;
    uint32_t reconnects;
    uint32_t heartbeat_failures;
    char result[48];
    uint32_t last_response_command;
    uint16_t last_frame_length;
    char last_response_type[12];
    char last_failure[28];
  };

  void begin(NetworkManager* network);
  Snapshot snapshot() const;
  Accept request(Request request, uint32_t* command_id = nullptr);
  void enableReadsFromUsb();
  bool enableControlFromUsb();
  static const char* relayName(Relay value);
  static const char* progressName(Progress value);
  static const char* acceptName(Accept value);

 private:
  static void taskEntry(void* context);
  void run();
  bool readStatus();
  bool connectSession();
  void sessionFailure(const char* failure);
  void applyPush(const String& json);
  static void pushEntry(void* context, const String& json);
  void recordProtocol(bool valid, const char* failure);
  void execute(Request request);
  bool persistLease(int64_t now);
  void clearLease();
  void setProgress(Progress progress, const char* result);
  bool online() const;

  NetworkManager* network_ = nullptr;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot state_{};
  QueueHandle_t queue_ = nullptr;
  bool pending_ = false;
  bool storage_ready_ = false;
  bool task_ready_ = false;
  bool lease_valid_ = false;
  bool monitor_armed_ = false;
  uint8_t reconnect_failures_ = 0;
  uint32_t next_reconnect_ms_ = 0;
  uint32_t next_reconcile_ms_ = 0;
  uint32_t next_heartbeat_ms_ = 0;
  LynkTuyaDevice<TUYA_V34>* device_ = nullptr;
  int64_t lease_deadline_ = 0;
  uint32_t lease_started_ms_ = 0;
  Preferences preferences_;
};
#endif
