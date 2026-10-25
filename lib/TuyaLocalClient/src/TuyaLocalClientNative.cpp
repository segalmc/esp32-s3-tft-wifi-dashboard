#include "TuyaLocalClient.h"
#include <cctype>
#include <cstring>

void TuyaLocalClient::setPhase(uint8_t phase) {
  phase_ = static_cast<Phase>(phase);
  phaseStartedMs_ = millis();
}

bool TuyaLocalClient::timedOut(uint32_t now, uint32_t timeout) const {
  return static_cast<uint32_t>(now - phaseStartedMs_) >= timeout;
}

bool TuyaLocalClient::validDpsId(const char* id) const {
  if (!id || !*id) return false;
  size_t size = 0;
  for (const char* p = id; *p; ++p) {
    if (++size > 5 || !isdigit(static_cast<unsigned char>(*p))) return false;
  }
  return true;
}

bool TuyaLocalClient::requestStatus() {
  if (!native_ || !device_ || inPoll_ || inCallback_ || pending_ != None ||
      phase_ == StatusSending || phase_ == StatusWaiting ||
      phase_ == ControlSending || phase_ == ControlWaiting) return false;
  pending_ = Status;
  pendingStatusExplicit_ = true;
  statusResultReady_ = false;
  return true;
}

bool TuyaLocalClient::requestBooleanDps(const char* id, bool value, uint32_t timestamp) {
  if (!native_ || !device_ || inPoll_ || inCallback_ || !validDpsId(id) ||
      pending_ != None || phase_ == ControlSending || phase_ == ControlWaiting ||
      (value && (phase_ != Ready || !connected()))) return false;
  if (!value && (phase_ == StatusSending || phase_ == StatusWaiting ||
                 phase_ == HeartbeatSending || phase_ == HeartbeatWaiting))
    disconnect();  // OFF has priority over background work; one socket only.
  memcpy(pendingDpsId_, id, strlen(id) + 1);
  pendingValue_ = value;
  pendingTimestamp_ = timestamp;
  pending_ = Control;
  commandResultReady_ = false;
  if (phase_ == Backoff) { phase_ = Disconnected; nextReconnectMs_ = millis(); }
  return true;
}

bool TuyaLocalClient::takeStatusResult(String& jsonOut, bool& succeeded) {
  if (!statusResultReady_) return false;
  statusResultReady_ = false;
  succeeded = statusResultSuccess_;
  jsonOut = succeeded ? completedStatus_ : String();
  completedStatus_ = "";
  return true;
}

bool TuyaLocalClient::takeCommandResult(bool& acknowledged) {
  if (!commandResultReady_) return false;
  commandResultReady_ = false;
  acknowledged = commandResultAck_;
  return true;
}

void TuyaLocalClient::finishStatus(bool success, const String& json) {
  statusResultReady_ = true;
  statusResultSuccess_ = success;
  completedStatus_ = success ? json : String();
  if (!success) return;
  nextReconcileMs_ = millis() + config_.statusReconciliationMs;
  inCallback_ = true;
  mappedUpdate(json, false);
  if (!activeStatusExplicit_ && callbacks_.onStatus)
    callbacks_.onStatus(callbacks_.context, json);
  inCallback_ = false;
}

void TuyaLocalClient::finishCommand(bool acknowledged) {
  commandResultReady_ = true;
  commandResultAck_ = acknowledged;
  if (callbacks_.onCommand) {
    inCallback_ = true;
    callbacks_.onCommand(callbacks_.context, pendingDpsId_, pendingValue_, acknowledged);
    inCallback_ = false;
  }
}

TuyaLocalClient::PollResult TuyaLocalClient::abortNative(Error error, bool heartbeatFailure) {
  const bool controlActive = phase_ == ControlSending || phase_ == ControlWaiting || pending_ == Control;
  const bool statusActive = phase_ == StatusSending || phase_ == StatusWaiting || pending_ == Status;
  device_->setTransportError(error);
  if (heartbeatFailure && config_.diagnostics.countEvents) ++diagnostics_.heartbeatFailures;
  failed();
  capture(false);
  if (controlActive) finishCommand(false);
  if (statusActive) finishStatus(false, String());
  return failureResult();
}

TuyaLocalClient::PollResult TuyaLocalClient::advanceNative() {
  const uint32_t now = millis();
  if (phase_ == Backoff) {
    if (static_cast<int32_t>(now - nextReconnectMs_) < 0) return PollResult::NoFrame;
    setPhase(Disconnected);
  }
  if (phase_ == Disconnected) {
    if (static_cast<int32_t>(now - nextReconnectMs_) < 0) return PollResult::NoFrame;
    if (!socket_.start(config_.host, config_.port)) return abortNative(Error::Connect);
    setPhase(Connecting);
    return PollResult::NoFrame;
  }
  if (phase_ == Connecting) {
    const auto state = socket_.progressConnect();
    if (state == TuyaSocketTransport::State::Failed) return abortNative(Error::Connect);
    if (state == TuyaSocketTransport::State::Connecting)
      return timedOut(now, config_.connectionTimeoutMs) ?
          abortNative(Error::Timeout) : PollResult::NoFrame;
    if (!device_->startHandshake()) return abortNative(device_->lastError());
    setPhase(HandshakeSending);
    return PollResult::NoFrame;
  }
  if (phase_ == HandshakeSending || phase_ == HandshakeConfirmSending ||
      phase_ == StatusSending || phase_ == HeartbeatSending || phase_ == ControlSending) {
    if (socket_.txPending()) return timedOut(now, config_.commandResponseTimeoutMs) ?
        abortNative(Error::Timeout, phase_ == HeartbeatSending) : PollResult::NoFrame;
    if (phase_ == HandshakeSending) setPhase(HandshakeWaiting);
    else if (phase_ == HandshakeConfirmSending) {
      failures_ = 0;
      diagnostics_.scheduledReconnectDelayMs = 0;
      if (config_.diagnostics.countEvents) ++diagnostics_.sessions;
      nextHeartbeatMs_ = now + config_.heartbeatIntervalMs;
      nextReconcileMs_ = now;
      setPhase(Ready);
      connectionChanged(true);
    } else if (phase_ == StatusSending) setPhase(StatusWaiting);
    else if (phase_ == HeartbeatSending) setPhase(HeartbeatWaiting);
    else setPhase(ControlWaiting);
    return PollResult::NoFrame;
  }
  if (phase_ == HandshakeWaiting) {
    if (timedOut(now, config_.commandResponseTimeoutMs)) return abortNative(Error::Timeout);
    const auto result = device_->advanceHandshake();
    if (result == Protocol::AsyncResult::Pending) return PollResult::NoFrame;
    if (result == Protocol::AsyncResult::Invalid) return abortNative(device_->lastError());
    capture(true);
    setPhase(HandshakeConfirmSending);
    return PollResult::NoFrame;
  }
  if (phase_ == StatusWaiting || phase_ == HeartbeatWaiting || phase_ == ControlWaiting) {
    if (timedOut(now, config_.commandResponseTimeoutMs))
      return abortNative(Error::Timeout, phase_ == HeartbeatWaiting);
    const uint32_t expected = phase_ == StatusWaiting ? 0x10 :
                              phase_ == HeartbeatWaiting ? 0x09 : 0x0d;
    String json;
    const auto result = device_->advanceResponse(expected, expected == 0x10 ? &json : nullptr);
    if (result == Protocol::AsyncResult::Pending) return PollResult::NoFrame;
    if (result == Protocol::AsyncResult::Push) return PollResult::Frame;
    if (result == Protocol::AsyncResult::Invalid)
      return abortNative(device_->lastError(), phase_ == HeartbeatWaiting);
    const Phase completed = phase_;
    setPhase(Ready);
    capture(true);
    if (completed == StatusWaiting) finishStatus(true, json);
    else if (completed == ControlWaiting) finishCommand(true);
    else nextHeartbeatMs_ = now + config_.heartbeatIntervalMs;
    return PollResult::Frame;
  }
  if (phase_ != Ready) return PollResult::NoFrame;
  // Dispatch an authenticated unsolicited event before scheduling background
  // work. The read budget and frame count bound work in this call.
  const PollResult incoming = device_->pollFrame();
  if (incoming == PollResult::Invalid || incoming == PollResult::Disconnected)
    return abortNative(device_->lastError());
  if (incoming == PollResult::Frame) return incoming;
  if (pending_ == Control) {
    if (!device_->startBooleanDps(pendingDpsId_, pendingValue_, pendingTimestamp_))
      return abortNative(device_->lastError());
    pending_ = None;
    setPhase(ControlSending);
  } else if (pending_ == Status) {
    activeStatusExplicit_ = pending_ == Status && pendingStatusExplicit_;
    pending_ = None;
    statusResultReady_ = false;
    if (!device_->startStatus()) return abortNative(device_->lastError());
    setPhase(StatusSending);
  } else if (static_cast<int32_t>(now - nextHeartbeatMs_) >= 0) {
    if (!device_->startHeartbeat()) return abortNative(device_->lastError(), true);
    setPhase(HeartbeatSending);
  } else if (static_cast<int32_t>(now - nextReconcileMs_) >= 0) {
    activeStatusExplicit_ = false;
    statusResultReady_ = false;
    if (!device_->startStatus()) return abortNative(device_->lastError());
    setPhase(StatusSending);
  }
  return PollResult::NoFrame;
}

TuyaLocalClient::PollResult TuyaLocalClient::pollNative(bool networkAvailable) {
  if (!device_ || inPoll_) return PollResult::Disconnected;
  if (!networkAvailable) {
    const bool controlActive = phase_ == ControlSending || phase_ == ControlWaiting || pending_ == Control;
    const bool statusActive = phase_ == StatusSending || phase_ == StatusWaiting || pending_ == Status;
    disconnect();
    if (controlActive) finishCommand(false);
    if (statusActive) finishStatus(false, String());
    return PollResult::Disconnected;
  }
  inPoll_ = true;
  size_t remainingBytes = config_.maxBytesPerPoll;
  if (socket_.state() == TuyaSocketTransport::State::Open) {
    const int sent = socket_.serviceTx(remainingBytes);
    if (sent < 0) {
      const PollResult result = abortNative(Error::Connect);
      inPoll_ = false;
      return result;
    }
    remainingBytes -= static_cast<size_t>(sent);
    if (!socket_.connected()) {
      const PollResult result = abortNative(Error::Connect);
      inPoll_ = false;
      return result;
    }
  }
  device_->setReadBudget(remainingBytes);
  PollResult last = PollResult::NoFrame;
  uint8_t frames = 0;
  for (uint8_t advanced = 0; advanced < config_.maxTransactionsPerPoll; ++advanced) {
    const Phase before = phase_;
    last = advanceNative();
    if (last == PollResult::Invalid || last == PollResult::Disconnected) break;
    if (last == PollResult::Frame && ++frames >= config_.maxFramesPerPoll) break;
    if (last == PollResult::NoFrame && phase_ == before) break;
  }
  inPoll_ = false;
  if (deferredInvalidation_) {
    deferredInvalidation_ = false;
    failed();
    return PollResult::Invalid;
  }
  return last;
}
