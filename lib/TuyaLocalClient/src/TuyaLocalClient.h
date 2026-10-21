#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <LynkTuya.h>
#include "TuyaSocketTransport.h"

// Fixed at begin(); no credential or safety setting has a runtime setter.
struct TuyaLocalConfig {
  enum class ProtocolVersion : uint8_t { V34 = 34, V35 = 35 };
  enum class LogLevel : uint8_t { Silent, Errors, ProtocolMetadata };
  struct DpsMapping {
    uint16_t stateBoolean = 1;
    uint16_t controlBoolean = 1;
  };
  struct RetryPolicy {
    uint8_t statusReadAttempts = 1;
    uint8_t controlWriteAttempts = 1;  // Must remain one: uncertain ON is never resent.
  };
  struct DiagnosticsOptions {
    bool countEvents = true;
    bool captureResponseMetadata = true;
  };

  IPAddress host;
  uint16_t port = 6668;
  const char* deviceId = nullptr;
  const char* localKey = nullptr;
  ProtocolVersion protocol = ProtocolVersion::V34;
  DpsMapping dps;
  uint32_t heartbeatIntervalMs = 10000;
  uint32_t statusReconciliationMs = 30000;
  uint32_t connectionTimeoutMs = 3000;
  uint32_t commandResponseTimeoutMs = 3000;
  uint32_t partialFrameTimeoutMs = 300;
  uint32_t reconnectMinimumMs = 1000;
  uint32_t reconnectMaximumMs = 32000;
  uint16_t maxBytesPerPoll = 256;
  uint8_t maxFramesPerPoll = 1;
  uint8_t maxTransactionsPerPoll = 2;
  RetryPolicy retry;
  LogLevel logging = LogLevel::Silent;
  DiagnosticsOptions diagnostics;
};

// One owner must call every method, including poll(), serially. The library
// neither creates a task nor decides whether a relay command is safe.
class TuyaLocalClient {
 public:
  using Protocol = LynkTuyaDevice<TUYA_V34>;
  using Error = Protocol::Error;
  using PollResult = Protocol::PollResult;
  using Config = TuyaLocalConfig;
  struct Diagnostics {
    bool connected = false;
    uint32_t sessions = 0;
    uint32_t pushEvents = 0;
    uint32_t heartbeatFailures = 0;
    uint32_t connectionErrors = 0;
    uint32_t authenticationErrors = 0;
    uint32_t invalidResponses = 0;
    uint32_t scheduledReconnectDelayMs = 0;
    uint32_t lastCommand = 0;
    uint16_t lastFrameLength = 0;
    Protocol::ResponseType lastResponseType = Protocol::ResponseType::None;
    Error lastError = Error::None;
    uint32_t lastPollUs = 0;
    uint32_t maxPollUs = 0;
  };
  using JsonCallback = void (*)(void* context, const String& json);
  using ConnectionCallback = void (*)(void* context, bool connected);
  // A command result means the exchange completed, not that a DPS state was verified.
  using CommandCallback = void (*)(void* context, const char* dpsId, bool requested, bool acknowledged);
  struct DiagnosticEvent {
    uint32_t command;
    uint16_t frameLength;
    Protocol::ResponseType responseType;
    Error error;
  };
  using DiagnosticCallback = void (*)(void* context, const DiagnosticEvent& event);
  using BooleanCallback = void (*)(void* context, uint16_t dpsId, bool value, bool push);
  struct Callbacks {
    JsonCallback onStatus = nullptr;
    JsonCallback onPush = nullptr;
    ConnectionCallback onConnection = nullptr;
    CommandCallback onCommand = nullptr;
    BooleanCallback onMappedBoolean = nullptr;
    DiagnosticCallback onDiagnostic = nullptr;
    void* context = nullptr;
  };

  TuyaLocalClient() = default;
  ~TuyaLocalClient();
  TuyaLocalClient(const TuyaLocalClient&) = delete;
  TuyaLocalClient& operator=(const TuyaLocalClient&) = delete;

  // Config strings are copied. Optional transport is borrowed for host mocks.
  // begin() only configures the client; connect()/poll() initiate network I/O.
  bool begin(const Config& config, const Callbacks& callbacks, Client* transport = nullptr);
  // Cooperative socket injection is useful for deterministic host tests;
  // the borrowed transport must obey the nonblocking contract.
  bool beginCooperative(const Config& config, const Callbacks& callbacks,
                        TuyaSocketTransport& transport);
  bool begin(const Config& config) { return begin(config, Callbacks{}, nullptr); }
  void end();
  bool connect();
  void disconnect();
  // Reject an application-invalid status and back off before reconnecting.
  void invalidate();
  bool connected();
  // Native IPv4 mode uses one nonblocking socket. Injected Client compatibility
  // mode retains the injected transport's (possibly blocking) behavior.
  PollResult poll(bool networkAvailable = true);
  // Native mode only: enqueue one protocol transaction. A successful request
  // means accepted, not that a relay state has been verified. Completion is
  // reported by callbacks and takeStatusResult/takeCommandResult.
  bool requestStatus();
  bool requestBooleanDps(const char* dpsId, bool value, uint32_t timestamp);
  bool takeStatusResult(String& jsonOut, bool& succeeded);
  bool takeCommandResult(bool& acknowledged);
  bool readStatus(String& jsonOut);
  bool readMappedBoolean(bool& value);
  bool reconnect(String& jsonOut);
  bool setBooleanDps(const char* dpsId, bool value, uint32_t timestamp);
  bool setMappedBoolean(bool value, uint32_t timestamp);
  Diagnostics diagnostics() const { return diagnostics_; }
  static const char* errorName(Error error) { return Protocol::errorName(error); }
  static const char* responseTypeName(Protocol::ResponseType type) {
    return Protocol::responseTypeName(type);
  }

 private:
  bool beginImpl(const Config& config, const Callbacks& callbacks,
                 Client* legacyTransport, TuyaSocketTransport* nativeTransport);
  static void pushEntry(void* context, const String& json);
  void pushed(const String& json);
  void capture(bool success);
  void failed();
  void connectionChanged(bool connected);
  uint32_t retryDelay(uint8_t failures) const;
  bool parseMappedBoolean(const String& json, bool& value) const;
  void mappedUpdate(const String& json, bool push);
  PollResult failureResult() const;
  PollResult pollNative(bool networkAvailable);
  PollResult advanceNative();
  void finishStatus(bool success, const String& json);
  void finishCommand(bool acknowledged);
  bool timedOut(uint32_t now, uint32_t timeout) const;
  void setPhase(uint8_t phase);
  bool validDpsId(const char* dpsId) const;
  PollResult abortNative(Error error, bool heartbeatFailure = false);

  enum Phase : uint8_t {
    Disconnected, Connecting, HandshakeSending, HandshakeWaiting,
    HandshakeConfirmSending, Ready, StatusSending, StatusWaiting,
    HeartbeatSending, HeartbeatWaiting, ControlSending, ControlWaiting, Backoff
  };
  enum Pending : uint8_t { None, Status, Control };

  Protocol* device_ = nullptr;
  TuyaSocketTransport socket_;
  TuyaSocketTransport* activeSocket_ = &socket_;
  bool native_ = false;
  Phase phase_ = Disconnected;
  Pending pending_ = None;
  uint32_t phaseStartedMs_ = 0;
  uint32_t transactionStartedMs_ = 0;
  bool pendingStatusExplicit_ = false;
  bool activeStatusExplicit_ = false;
  char pendingDpsId_[6] = {};
  bool pendingValue_ = false;
  uint32_t pendingTimestamp_ = 0;
  String completedStatus_;
  bool statusResultReady_ = false;
  bool statusResultSuccess_ = false;
  bool commandResultReady_ = false;
  bool commandResultAck_ = false;
  bool inPoll_ = false;
  bool inCallback_ = false;
  bool deferredInvalidation_ = false;
  char deviceId_[33] = {};
  char localKey_[17] = {};
  Config config_{};
  Callbacks callbacks_{};
  Diagnostics diagnostics_{};
  uint8_t failures_ = 0;
  uint32_t nextReconnectMs_ = 0;
  uint32_t nextHeartbeatMs_ = 0;
  uint32_t nextReconcileMs_ = 0;
  bool reportedConnected_ = false;
};
