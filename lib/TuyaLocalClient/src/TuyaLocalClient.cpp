#include "TuyaLocalClient.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cstring>

TuyaLocalClient::~TuyaLocalClient() { end(); }

bool TuyaLocalClient::begin(const Config& config, const Callbacks& callbacks, Client* transport) {
  end();
  if (config.host == IPAddress(0, 0, 0, 0) || !config.port ||
      config.protocol != Config::ProtocolVersion::V34 ||
      !config.deviceId || !*config.deviceId ||
      strnlen(config.deviceId, sizeof(deviceId_)) >= sizeof(deviceId_) ||
      !config.localKey || strnlen(config.localKey, 17) != 16 ||
      !config.dps.stateBoolean || config.dps.stateBoolean > 9999 ||
      !config.dps.controlBoolean || config.dps.controlBoolean > 9999 ||
      config.heartbeatIntervalMs < 1000 || config.heartbeatIntervalMs > 60000 ||
      config.statusReconciliationMs < 1000 || config.statusReconciliationMs > 300000 ||
      config.connectionTimeoutMs < 100 || config.connectionTimeoutMs > 10000 ||
      config.commandResponseTimeoutMs < 100 || config.commandResponseTimeoutMs > 10000 ||
      config.partialFrameTimeoutMs < 100 || config.partialFrameTimeoutMs > 10000 ||
      config.reconnectMinimumMs < 250 || config.reconnectMinimumMs > 32000 ||
      config.reconnectMaximumMs < config.reconnectMinimumMs ||
      config.reconnectMaximumMs > 60000 ||
      config.maxBytesPerPoll < 16 || config.maxBytesPerPoll > 1024 ||
      config.maxFramesPerPoll < 1 || config.maxFramesPerPoll > 4 ||
      config.maxTransactionsPerPoll < 1 || config.maxTransactionsPerPoll > 8 ||
      config.retry.statusReadAttempts < 1 || config.retry.statusReadAttempts > 3 ||
      config.retry.controlWriteAttempts != 1 ||
      static_cast<uint8_t>(config.logging) >
          static_cast<uint8_t>(Config::LogLevel::ProtocolMetadata)) return false;
  for (const char* p = config.deviceId; *p; ++p)
    if (!isalnum(static_cast<unsigned char>(*p)) && *p != '-' && *p != '_') return false;
  memcpy(deviceId_, config.deviceId, strlen(config.deviceId) + 1);
  memcpy(localKey_, config.localKey, 16);
  localKey_[16] = 0;
  config_ = config;
  config_.deviceId = deviceId_;
  config_.localKey = localKey_;
  callbacks_ = callbacks;
  native_ = transport == nullptr;
  device_ = new Protocol(config.host, localKey_, transport ? *transport : static_cast<Client&>(socket_));
  if (!device_) { end(); return false; }
  device_->setDeviceId(deviceId_);
  device_->setPort(config.port);
  device_->setConnectTimeout(config.connectionTimeoutMs);
  device_->setTimeout(config.commandResponseTimeoutMs);
  device_->setPartialFrameTimeout(config.partialFrameTimeoutMs);
  device_->onDpsUpdate(pushEntry, this);
  nextReconnectMs_ = millis();
  return true;
}

void TuyaLocalClient::end() {
  disconnect();
  delete device_;
  device_ = nullptr;
  memset(localKey_, 0, sizeof(localKey_));
  memset(deviceId_, 0, sizeof(deviceId_));
  config_ = Config{};
  callbacks_ = Callbacks{};
  diagnostics_ = Diagnostics{};
  failures_ = 0;
  nextReconnectMs_ = nextHeartbeatMs_ = nextReconcileMs_ = 0;
  native_ = false;
  phase_ = Disconnected;
  pending_ = None;
  statusResultReady_ = commandResultReady_ = false;
  inPoll_ = inCallback_ = deferredInvalidation_ = false;
}

bool TuyaLocalClient::connected() {
  return device_ && device_->sessionReady() &&
      (!native_ || (phase_ >= Ready && phase_ <= ControlWaiting));
}

void TuyaLocalClient::connectionChanged(bool connectedNow) {
  diagnostics_.connected = connectedNow;
  if (reportedConnected_ == connectedNow) return;
  reportedConnected_ = connectedNow;
  if (callbacks_.onConnection) {
    const bool wasInCallback = inCallback_;
    inCallback_ = true;
    callbacks_.onConnection(callbacks_.context, connectedNow);
    inCallback_ = wasInCallback;
  }
}

void TuyaLocalClient::disconnect() {
  if (device_) device_->closeConnection();
  socket_.stop();
  phase_ = Disconnected;
  pending_ = None;
  connectionChanged(false);
  nextReconnectMs_ = millis();
  diagnostics_.scheduledReconnectDelayMs = 0;
}

void TuyaLocalClient::invalidate() {
  if (inCallback_) { deferredInvalidation_ = true; return; }
  failed();
}

uint32_t TuyaLocalClient::retryDelay(uint8_t failures) const {
  uint32_t delayMs = config_.reconnectMinimumMs;
  for (uint8_t i = 0; i < failures && delayMs < config_.reconnectMaximumMs; ++i) {
    delayMs = delayMs > config_.reconnectMaximumMs / 2 ?
        config_.reconnectMaximumMs : delayMs * 2;
  }
  return delayMs;
}

TuyaLocalClient::PollResult TuyaLocalClient::failureResult() const {
  const Error error = diagnostics_.lastError;
  return error == Error::Connect || error == Error::Timeout ||
         error == Error::IncompleteFrame ? PollResult::Disconnected : PollResult::Invalid;
}

void TuyaLocalClient::capture(bool success) {
  if (!device_) return;
  if (config_.diagnostics.captureResponseMetadata) {
    diagnostics_.lastCommand = device_->lastResponseCommand();
    diagnostics_.lastFrameLength = static_cast<uint16_t>(device_->lastFrameLength());
    diagnostics_.lastResponseType = device_->lastResponseType();
  }
  diagnostics_.lastError = success ? Error::None : device_->lastError();
  if (callbacks_.onDiagnostic &&
      (config_.logging == Config::LogLevel::ProtocolMetadata ||
       (!success && config_.logging == Config::LogLevel::Errors))) {
    const DiagnosticEvent event{device_->lastResponseCommand(),
        static_cast<uint16_t>(device_->lastFrameLength()),
        device_->lastResponseType(), diagnostics_.lastError};
    const bool wasInCallback = inCallback_;
    inCallback_ = true;
    callbacks_.onDiagnostic(callbacks_.context, event);
    inCallback_ = wasInCallback;
  }
  if (success) return;
  if (!config_.diagnostics.countEvents) return;
  if (diagnostics_.lastError == Error::Connect || diagnostics_.lastError == Error::Timeout ||
      diagnostics_.lastError == Error::IncompleteFrame) ++diagnostics_.connectionErrors;
  else if (diagnostics_.lastError == Error::Authentication)
    ++diagnostics_.authenticationErrors;
  else ++diagnostics_.invalidResponses;
}

void TuyaLocalClient::failed() {
  disconnect();
  if (failures_ < 6) ++failures_;
  diagnostics_.scheduledReconnectDelayMs = retryDelay(failures_ - 1);
  nextReconnectMs_ = millis() + diagnostics_.scheduledReconnectDelayMs;
}

bool TuyaLocalClient::connect() {
  if (!device_) return false;
  if (native_) {
    if (connected()) return true;
    nextReconnectMs_ = millis();
    if (phase_ == Backoff) phase_ = Disconnected;
    const uint32_t started = millis();
    const uint32_t limit = config_.connectionTimeoutMs + 2 * config_.commandResponseTimeoutMs;
    while (static_cast<uint32_t>(millis() - started) < limit) {
      const PollResult result = poll(true);
      if (connected()) return true;
      if (result == PollResult::Invalid || result == PollResult::Disconnected) return false;
      delay(1);
    }
    device_->setTransportError(Error::Timeout);
    capture(false); failed();
    return false;
  }
  if (connected()) return true;
  disconnect();
  if (!device_->connectToPlug() || !device_->handshake()) {
    capture(false);
    failed();
    return false;
  }
  capture(true);
  failures_ = 0;
  diagnostics_.scheduledReconnectDelayMs = 0;
  if (config_.diagnostics.countEvents) ++diagnostics_.sessions;
  nextHeartbeatMs_ = millis() + config_.heartbeatIntervalMs;
  nextReconcileMs_ = millis();
  connectionChanged(true);
  return true;
}

bool TuyaLocalClient::readStatus(String& jsonOut) {
  jsonOut = "";
  if (native_) {
    for (uint8_t attempt = 0; attempt < config_.retry.statusReadAttempts; ++attempt) {
      if (!connect()) continue;
      const bool alreadyReading = phase_ == StatusSending || phase_ == StatusWaiting;
      if (!alreadyReading && !requestStatus()) return false;
      const uint32_t started = millis();
      const uint32_t limit = config_.commandResponseTimeoutMs * 2;
      while (static_cast<uint32_t>(millis() - started) < limit) {
        poll(true);
        bool succeeded = false;
        if (takeStatusResult(jsonOut, succeeded)) return succeeded;
        if (!connected()) break;
        delay(1);
      }
      device_->setTransportError(Error::Timeout);
      capture(false); failed();
    }
    return false;
  }
  for (uint8_t attempt = 0; attempt < config_.retry.statusReadAttempts; ++attempt) {
    if (!connect()) continue;
    if (device_->getStatus(jsonOut, false)) {
      capture(true);
      nextReconcileMs_ = millis() + config_.statusReconciliationMs;
      mappedUpdate(jsonOut, false);
      return true;
    }
    capture(false);
    failed();
    jsonOut = "";
  }
  return false;
}

bool TuyaLocalClient::parseMappedBoolean(const String& json, bool& value) const {
  StaticJsonDocument<768> document;
  if (deserializeJson(document, json.c_str())) return false;
  char id[6];
  snprintf(id, sizeof(id), "%u", static_cast<unsigned>(config_.dps.stateBoolean));
  JsonVariant mapped = document["dps"][id];
  if (mapped.isNull()) mapped = document["data"]["dps"][id];
  if (!mapped.is<bool>()) return false;
  value = mapped.as<bool>();
  return true;
}

void TuyaLocalClient::mappedUpdate(const String& json, bool push) {
  if (!callbacks_.onMappedBoolean) return;
  bool value = false;
  if (parseMappedBoolean(json, value))
    callbacks_.onMappedBoolean(callbacks_.context, config_.dps.stateBoolean, value, push);
}

bool TuyaLocalClient::readMappedBoolean(bool& value) {
  String json;
  return readStatus(json) && parseMappedBoolean(json, value);
}

bool TuyaLocalClient::reconnect(String& jsonOut) {
  disconnect();
  return readStatus(jsonOut);
}

bool TuyaLocalClient::setBooleanDps(const char* dpsId, bool value, uint32_t timestamp) {
  bool validId = dpsId && *dpsId;
  if (validId) {
    size_t length = 0;
    for (const char* p = dpsId; *p; ++p) {
      ++length;
      if (length > 5 || !isdigit(static_cast<unsigned char>(*p))) { validId = false; break; }
    }
  }
  if (native_) {
    if (!validId) return false;
    // A safety OFF supersedes background status/heartbeat exchanges. Closing
    // the old socket avoids ambiguous response matching before reconnecting.
    if (!value && (phase_ == StatusSending || phase_ == StatusWaiting ||
                   phase_ == HeartbeatSending || phase_ == HeartbeatWaiting))
      disconnect();
    if (!connect()) {
      if (callbacks_.onCommand) callbacks_.onCommand(callbacks_.context, dpsId, value, false);
      return false;
    }
    if (value) {
      const uint32_t started = millis();
      while (phase_ != Ready && connected() &&
             static_cast<uint32_t>(millis() - started) < config_.commandResponseTimeoutMs) {
        poll(true);
        delay(1);
      }
    }
    if (!requestBooleanDps(dpsId, value, timestamp)) {
      if (callbacks_.onCommand) callbacks_.onCommand(callbacks_.context, dpsId, value, false);
      return false;
    }
    const uint32_t started = millis();
    const uint32_t limit = config_.commandResponseTimeoutMs * 2;
    while (static_cast<uint32_t>(millis() - started) < limit) {
      poll(true);
      bool acknowledged = false;
      if (takeCommandResult(acknowledged)) return acknowledged;
      if (!connected()) break;
      delay(1);
    }
    device_->setTransportError(Error::Timeout);
    capture(false); failed();
    return false;
  }
  if (!validId || !connect()) {
    if (callbacks_.onCommand) callbacks_.onCommand(callbacks_.context, dpsId, value, false);
    return false;
  }
  const bool acknowledged = device_->setDps(dpsId, value, false, timestamp);
  capture(acknowledged);
  if (callbacks_.onCommand) callbacks_.onCommand(callbacks_.context, dpsId, value, acknowledged);
  return acknowledged;
}

bool TuyaLocalClient::setMappedBoolean(bool value, uint32_t timestamp) {
  char id[6];
  snprintf(id, sizeof(id), "%u", static_cast<unsigned>(config_.dps.controlBoolean));
  return setBooleanDps(id, value, timestamp);
}

void TuyaLocalClient::pushEntry(void* context, const String& json) {
  static_cast<TuyaLocalClient*>(context)->pushed(json);
}

void TuyaLocalClient::pushed(const String& json) {
  capture(true);
  if (config_.diagnostics.countEvents) ++diagnostics_.pushEvents;
  const bool wasInCallback = inCallback_;
  inCallback_ = true;
  mappedUpdate(json, true);
  if (callbacks_.onPush) callbacks_.onPush(callbacks_.context, json);
  inCallback_ = wasInCallback;
}

TuyaLocalClient::PollResult TuyaLocalClient::poll(bool networkAvailable) {
  const uint32_t startedUs = micros();
  if (native_) {
    const PollResult result = pollNative(networkAvailable);
    diagnostics_.lastPollUs = static_cast<uint32_t>(micros() - startedUs);
    if (diagnostics_.lastPollUs > diagnostics_.maxPollUs)
      diagnostics_.maxPollUs = diagnostics_.lastPollUs;
    return result;
  }
  if (!device_) return PollResult::Disconnected;
  if (!networkAvailable) {
    disconnect();
    nextReconnectMs_ = millis();
    return PollResult::Disconnected;
  }
  const uint32_t now = millis();
  if (!connected()) {
    connectionChanged(false);
    if (static_cast<int32_t>(now - nextReconnectMs_) < 0) return PollResult::NoFrame;
    if (!connect()) return failureResult();
    String json;
    if (!readStatus(json)) return failureResult();
    if (callbacks_.onStatus) callbacks_.onStatus(callbacks_.context, json);
    if (!connected()) return PollResult::Invalid;
    return PollResult::Frame;
  }
  const PollResult event = device_->pollFrame();
  if (event == PollResult::Invalid || event == PollResult::Disconnected) {
    capture(false);
    failed();
    return event;
  }
  if (static_cast<int32_t>(now - nextHeartbeatMs_) >= 0) {
    nextHeartbeatMs_ = millis() + config_.heartbeatIntervalMs;
    if (!device_->heartbeat(false)) {
      if (config_.diagnostics.countEvents) ++diagnostics_.heartbeatFailures;
      capture(false);
      failed();
      return failureResult();
    }
    capture(true);
  } else if (static_cast<int32_t>(now - nextReconcileMs_) >= 0) {
    String json;
    if (!readStatus(json)) return failureResult();
    if (callbacks_.onStatus) callbacks_.onStatus(callbacks_.context, json);
    if (!connected()) return PollResult::Invalid;
  }
  return event;
}
