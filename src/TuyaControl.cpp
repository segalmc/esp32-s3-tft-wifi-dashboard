#include "TuyaControl.h"
#if TFT_HARDWARE_ENABLED
#include "BoilerPolicy.h"
#include "BoilerDps.h"
#include "secrets.h"
#include <time.h>

namespace {
IPAddress targetIp() {
  IPAddress result;
  result.fromString(TUYA_DEVICE_IP);
  return result;
}
}  // namespace

void TuyaControl::begin(NetworkManager* network) {
  network_ = network;
  strncpy(state_.result, "idle", sizeof(state_.result));
  strncpy(state_.last_response_type, "none", sizeof(state_.last_response_type));
  strncpy(state_.last_failure, "none", sizeof(state_.last_failure));
  queue_ = xQueueCreate(1, sizeof(Request));
  task_ready_ = queue_ && xTaskCreate(taskEntry, "tuya-io", 10240, this, 1, nullptr) == pdPASS;
  if (!task_ready_)
    Serial.println("Tuya: task unavailable; control disabled");
}

void TuyaControl::taskEntry(void* context) { static_cast<TuyaControl*>(context)->run(); }

TuyaControl::Snapshot TuyaControl::snapshot() const {
  portENTER_CRITICAL(&lock_);
  Snapshot copy = state_;
  copy.busy = boiler_policy::busy(pending_, static_cast<uint8_t>(state_.progress));
  portEXIT_CRITICAL(&lock_);
  return copy;
}

bool TuyaControl::online() const {
  const auto wifi = network_->snapshot();
  return wifi.state == NetworkManager::State::Online && !wifi.ap_active &&
         WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

TuyaControl::Accept TuyaControl::request(Request request, uint32_t* command_id) {
  const auto wifi = network_->snapshot();
  const int64_t now = time(nullptr);
  const bool wifi_connected = WiFi.status() == WL_CONNECTED;
  portENTER_CRITICAL(&lock_);
  Accept answer = Accept::Accepted;
  if (boiler_policy::busy(pending_, static_cast<uint8_t>(state_.progress)))
    answer = Accept::Busy;
  else if (!task_ready_) answer = Accept::Disabled;
  else if (request == Request::DisarmMonitor && state_.lease_active)
    answer = Accept::LeaseActive;
  else if (request != Request::ArmMonitor && request != Request::DisarmMonitor &&
           (!state_.reads_enabled ||
            ((request == Request::On || request == Request::Off) && !state_.control_enabled)))
    answer = Accept::Disabled;
  else if (request != Request::ArmMonitor && request != Request::DisarmMonitor &&
           (wifi.state != NetworkManager::State::Online || wifi.ap_active ||
            !wifi_connected))
    answer = Accept::Offline;
  else if (request == Request::On && !boiler_policy::mayTurnOn(wifi.clock_synced, now, storage_ready_))
    answer = !storage_ready_ ? Accept::Storage : Accept::Clock;
  else if (request == Request::On && state_.lease_active)
    answer = Accept::LeaseActive;
  else if (request == Request::On && !boiler_policy::freshConfirmedOff(
               state_.relay == Relay::Off, state_.connected,
               state_.last_confirmed_ms, millis()))
    answer = Accept::StateUnknown;
  if (answer == Accept::Accepted) {
    boiler_policy::reserve(pending_, static_cast<uint8_t>(state_.progress));
    state_.progress = Progress::Pending;
    strncpy(state_.result, "queued", sizeof(state_.result));
    ++state_.command_id;
    if (command_id) *command_id = state_.command_id;
  }
  portEXIT_CRITICAL(&lock_);
  if (answer == Accept::Accepted && xQueueSend(queue_, &request, 0) != pdTRUE) {
    portENTER_CRITICAL(&lock_);
    pending_ = false;
    state_.progress = Progress::Failed;
    strncpy(state_.result, "queue unavailable", sizeof(state_.result));
    portEXIT_CRITICAL(&lock_);
    return Accept::Busy;
  }
  return answer;
}

void TuyaControl::enableReadsFromUsb() {
  portENTER_CRITICAL(&lock_);
  state_.reads_enabled = true;
  portEXIT_CRITICAL(&lock_);
}
bool TuyaControl::enableControlFromUsb() {
  portENTER_CRITICAL(&lock_);
  const bool allowed = state_.monitor_armed && state_.reads_enabled;
  if (allowed) state_.control_enabled = true;
  portEXIT_CRITICAL(&lock_);
  return allowed;
}

void TuyaControl::setProgress(Progress progress, const char* result) {
  portENTER_CRITICAL(&lock_);
  state_.progress = progress;
  strncpy(state_.result, result, sizeof(state_.result) - 1);
  state_.result[sizeof(state_.result) - 1] = 0;
  portEXIT_CRITICAL(&lock_);
}

bool TuyaControl::persistLease(int64_t now) {
  const auto lease = boiler_policy::makeLease(now);
  if (!storage_ready_ || !boiler_policy::saveLease(preferences_, lease)) return false;
  lease_valid_ = true;
  lease_ = lease;
  lease_started_ms_ = millis();
  portENTER_CRITICAL(&lock_);
  state_.lease_active = true;
  portEXIT_CRITICAL(&lock_);
  return true;
}

void TuyaControl::clearLease() {
  if (lease_valid_ && !preferences_.remove(boiler_config::kLeaseKey)) return;
  lease_valid_ = false;
  lease_ = {};
  lease_started_ms_ = 0;
  portENTER_CRITICAL(&lock_);
  state_.lease_active = false;
  portEXIT_CRITICAL(&lock_);
}

void TuyaControl::recordProtocol(bool valid, const char* failure) {
  const auto diagnostics = client_->diagnostics();
  const auto error = diagnostics.lastError;
  const char* response_type = TuyaLocalClient::responseTypeName(diagnostics.lastResponseType);
  portENTER_CRITICAL(&lock_);
  state_.last_response_command = diagnostics.lastCommand;
  state_.last_frame_length = diagnostics.lastFrameLength;
  snprintf(state_.last_response_type, sizeof(state_.last_response_type), "%s", response_type);
  snprintf(state_.last_failure, sizeof(state_.last_failure), "%s", valid ? "none" : failure);
  if (!valid) {
    ++state_.failures;
    if (error == TuyaLocalClient::Error::Connect ||
        error == TuyaLocalClient::Error::Timeout ||
        error == TuyaLocalClient::Error::IncompleteFrame)
      ++state_.connection_failures;
    else if (error == TuyaLocalClient::Error::Authentication)
      ++state_.authentication_failures;
    else ++state_.auth_or_parse_failures;
  }
  portEXIT_CRITICAL(&lock_);
  Serial.printf("Tuya protocol: cmd=0x%02lX frame_len=%u response=%s failure=%s\n",
                static_cast<unsigned long>(diagnostics.lastCommand),
                static_cast<unsigned>(diagnostics.lastFrameLength), response_type,
                valid ? "none" : failure);
}

void TuyaControl::sessionFailure(const char*) {
  client_->invalidate();
  portENTER_CRITICAL(&lock_);
  state_.connected = false;
  portEXIT_CRITICAL(&lock_);
}

bool TuyaControl::connectSession() {
  if (client_->connected()) return true;
  if (!online()) return false;
  if (!client_->connect()) {
    recordProtocol(false, TuyaLocalClient::errorName(client_->diagnostics().lastError));
    return false;
  }
  portENTER_CRITICAL(&lock_);
  state_.reconnects = client_->diagnostics().sessions;
  portEXIT_CRITICAL(&lock_);
  return true;
}

void TuyaControl::pushEntry(void* context, const String& json) {
  static_cast<TuyaControl*>(context)->applyPush(json);
}

void TuyaControl::statusEntry(void* context, const String& json) {
  static_cast<TuyaControl*>(context)->applyStatus(json);
}

void TuyaControl::applyStatus(const String& json) {
  bool on = false;
  boiler_dps::ParseError parse_error = boiler_dps::ParseError::None;
  if (!boiler_dps::parseRelay(json.c_str(), on, &parse_error)) {
    recordProtocol(false, parse_error == boiler_dps::ParseError::Json ?
                         "json-parsing" : "missing-or-invalid-dps1");
    sessionFailure("invalid DPS status");
    return;
  }
  portENTER_CRITICAL(&lock_);
  state_.relay = on ? Relay::On : Relay::Off;
  state_.connected = true;
  state_.last_confirmed_ms = millis();
  portEXIT_CRITICAL(&lock_);
  recordProtocol(true, "none");
}

void TuyaControl::applyPush(const String& json) {
  bool on = false;
  boiler_dps::ParseError parse_error = boiler_dps::ParseError::None;
  if (!boiler_dps::parseRelay(json.c_str(), on, &parse_error)) {
    if (parse_error == boiler_dps::ParseError::Json) recordProtocol(false, "json-parsing");
    return;  // Other DPS updates cannot establish the relay state.
  }
  portENTER_CRITICAL(&lock_);
  state_.relay = on ? Relay::On : Relay::Off;
  state_.connected = true;
  state_.last_confirmed_ms = millis();
  ++state_.push_events;
  portEXIT_CRITICAL(&lock_);
  recordProtocol(true, "none");
}

bool TuyaControl::readStatus() {
  if (!connectSession()) return false;
  String json;
  const bool received = client_->readStatus(json);
  bool on = false;
  boiler_dps::ParseError parse_error = boiler_dps::ParseError::None;
  const bool valid = received && boiler_dps::parseRelay(json.c_str(), on, &parse_error);
  const char* failure = received ?
      (parse_error == boiler_dps::ParseError::Json ? "json-parsing" :
       parse_error == boiler_dps::ParseError::MissingOrInvalidDps1 ? "missing-or-invalid-dps1" : "none") :
      TuyaLocalClient::errorName(client_->diagnostics().lastError);
  json = "";
  portENTER_CRITICAL(&lock_);
  state_.connected = valid;
  if (valid) {
    state_.relay = on ? Relay::On : Relay::Off;
    state_.last_confirmed_ms = millis();
  }
  portEXIT_CRITICAL(&lock_);
  recordProtocol(valid, failure);
  if (!valid) sessionFailure(failure);
  return valid;
}

void TuyaControl::execute(Request request) {
  if (request == Request::ArmMonitor) {
    const bool saved = storage_ready_ && boiler_policy::armMonitor(preferences_);
    if (!saved) { setProgress(Progress::Failed, "monitor authorization not persisted"); return; }
    monitor_armed_ = true;
    portENTER_CRITICAL(&lock_);
    state_.monitor_armed = true;
    state_.reads_enabled = true;
    portEXIT_CRITICAL(&lock_);
    setProgress(Progress::Confirmed, "monitor armed for future boots");
    return;
  }
  if (request == Request::DisarmMonitor) {
    const bool removed = storage_ready_ && boiler_policy::disarmMonitor(preferences_);
    if (!removed) { setProgress(Progress::Failed, "monitor authorization not removed"); return; }
    monitor_armed_ = false;
    client_->disconnect();
    portENTER_CRITICAL(&lock_);
    state_.monitor_armed = false;
    state_.reads_enabled = false;
    state_.control_enabled = false;
    state_.connected = false;
    portEXIT_CRITICAL(&lock_);
    setProgress(Progress::Confirmed, "monitor disarmed");
    return;
  }
  if (request == Request::Status || request == Request::Reconnect) {
    if (request == Request::Reconnect) client_->disconnect();
    setProgress(Progress::Verifying, "reading authenticated state");
    if (readStatus()) setProgress(Progress::Confirmed, "state confirmed");
    else setProgress(Progress::Failed, "status unavailable or invalid");
    return;
  }
  const bool target_on = request == Request::On;
  const int64_t now = time(nullptr);
  if (target_on) {
    const auto wifi = network_->snapshot();
    if (!boiler_policy::mayTurnOn(wifi.clock_synced, now, storage_ready_)) {
      setProgress(Progress::Failed, "clock or storage unavailable");
      return;
    }
    setProgress(Progress::Verifying, "confirming OFF before ON");
    if (!readStatus() || snapshot().relay != Relay::Off) {
      setProgress(Progress::Failed, "relay is not confirmed OFF");
      return;
    }
    if (!persistLease(now)) {
      setProgress(Progress::Failed, "deadline persistence failed");
      return;
    }
  }
  setProgress(Progress::Sending, target_on ? "sending ON once" : "sending OFF");
  const bool ack = connectSession() &&
      client_->setBooleanDps("1", target_on, static_cast<uint32_t>(now));
  // An ACK cannot establish relay state. Even an absent ACK may follow an applied command.
  setProgress(Progress::Verifying, "verifying authenticated relay state");
  bool matched = false;
  for (uint8_t attempt = 0; attempt < boiler_config::kActive.verificationReadAttempts; ++attempt) {
    if (readStatus()) {
      const auto state = snapshot();
      if (boiler_policy::verified(target_on, true, state.relay == Relay::On)) {
        matched = true;
        break;
      }
    }
    if (attempt + 1 < boiler_config::kActive.verificationReadAttempts)
      vTaskDelay(pdMS_TO_TICKS(boiler_config::kActive.verificationReadDelayMs));
  }
  if (matched) {
    if (!target_on) clearLease();
    setProgress(Progress::Confirmed, target_on ? "ON verified" : "OFF verified");
  }
  else setProgress(Progress::Uncertain, ack ? "state did not verify" : "command outcome uncertain");
  // Never resend an uncertain ON. The persisted lease remains until OFF is verified.
}

void TuyaControl::run() {
  TuyaLocalClient client;
  TuyaLocalConfig config;
  config.host = targetIp();
  config.deviceId = TUYA_DEVICE_ID;
  config.localKey = TUYA_LOCAL_KEY;
  config.commandResponseTimeoutMs = 3000;
  config.heartbeatIntervalMs = boiler_policy::kHeartbeatMs;
  config.statusReconciliationMs = boiler_policy::kReconcileMs;
  config.dps.stateBoolean = 1;
  config.dps.controlBoolean = 1;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onStatus = statusEntry;
  callbacks.onPush = pushEntry;
  callbacks.context = this;
  const bool client_ready = client.begin(config, callbacks);
  client_ = &client;
  portENTER_CRITICAL(&lock_);
  state_.reconcile_interval_ms = config.statusReconciliationMs;
  portEXIT_CRITICAL(&lock_);
  if (!client_ready) Serial.println("Tuya: client configuration unavailable");
  storage_ready_ = preferences_.begin(boiler_config::kNvsNamespace, false);
  portENTER_CRITICAL(&lock_);
  state_.storage_ready = storage_ready_;
  portEXIT_CRITICAL(&lock_);
  const auto gates = boiler_policy::bootGates(preferences_, storage_ready_);
  monitor_armed_ = gates.reads;
  portENTER_CRITICAL(&lock_);
  state_.monitor_armed = monitor_armed_;
  state_.reads_enabled = gates.reads;
  state_.control_enabled = gates.control;
  portEXIT_CRITICAL(&lock_);
  if (storage_ready_ && preferences_.isKey(boiler_config::kLeaseKey)) {
    boiler_policy::Lease lease{};
    if (boiler_policy::loadLease(preferences_, lease)) {
      lease_valid_ = true;
      lease_ = lease;
      portENTER_CRITICAL(&lock_);
      state_.lease_active = true;
      portEXIT_CRITICAL(&lock_);
    } else {
      Serial.println("Tuya: invalid persisted deadline; control disabled pending service");
      storage_ready_ = false;
      portENTER_CRITICAL(&lock_);
      state_.storage_ready = false;
      portEXIT_CRITICAL(&lock_);
    }
  }
  uint32_t next_safety = millis();
  for (;;) {
    Request request;
    if (queue_ && xQueueReceive(queue_, &request, pdMS_TO_TICKS(25)) == pdTRUE) {
      if (request == Request::ArmMonitor || request == Request::DisarmMonitor || online())
        execute(request);
      else setProgress(Progress::Failed, "Wi-Fi unavailable");
      const auto completed = snapshot();
      Serial.printf("Tuya: command id=%lu progress=%s relay=%s connection=%s result=%s\n",
                    static_cast<unsigned long>(completed.command_id),
                    progressName(completed.progress), relayName(completed.relay),
                    completed.connected ? "connected" : "unavailable", completed.result);
      portENTER_CRITICAL(&lock_);
      pending_ = false;
      portEXIT_CRITICAL(&lock_);
    }
    const uint32_t ms = millis();
    if (lease_valid_ && online() && boiler_policy::offRetryDue(ms, next_safety)) {
      const auto wifi = network_->snapshot();
      const int64_t now = time(nullptr);
      const bool monotonic_due = boiler_policy::monotonicLeaseDue(lease_started_ms_, ms);
      if (monotonic_due || boiler_policy::due(lease_, now, wifi.clock_synced)) {
        portENTER_CRITICAL(&lock_);
        const bool reserved = boiler_policy::reserve(pending_, static_cast<uint8_t>(state_.progress));
        portEXIT_CRITICAL(&lock_);
        if (reserved) {
          Serial.println("Tuya safety: shutdown obligation due; attempting OFF");
          execute(Request::Off);
          const auto completed = snapshot();
          Serial.printf("Tuya safety: progress=%s relay=%s result=%s\n",
                        progressName(completed.progress), relayName(completed.relay),
                        completed.result);
          portENTER_CRITICAL(&lock_);
          pending_ = false;
          portEXIT_CRITICAL(&lock_);
          next_safety = boiler_policy::nextOffRetryMs(millis());
        }
      }
    }
    const Snapshot current = snapshot();
    if (!online()) {
      client.disconnect();
      portENTER_CRITICAL(&lock_);
      state_.connected = false;
      portEXIT_CRITICAL(&lock_);
    } else if (current.reads_enabled &&
               !boiler_policy::busy(false, static_cast<uint8_t>(current.progress))) {
      const auto before = client.diagnostics();
      client.poll(true);
      const auto after = client.diagnostics();
      // Application DPS rejection is already recorded by applyStatus(); only
      // transport errors added by this poll need a diagnostic record.
      if (after.connectionErrors + after.authenticationErrors + after.invalidResponses >
          before.connectionErrors + before.authenticationErrors + before.invalidResponses)
        recordProtocol(false, TuyaLocalClient::errorName(after.lastError));
      portENTER_CRITICAL(&lock_);
      state_.reconnects = after.sessions;
      state_.heartbeat_failures = after.heartbeatFailures;
      if (!after.connected) state_.connected = false;
      portEXIT_CRITICAL(&lock_);
      if (boiler_policy::stale(snapshot().last_confirmed_ms, millis())) {
        portENTER_CRITICAL(&lock_);
        state_.connected = false;
        portEXIT_CRITICAL(&lock_);
      }
    } else if (!current.reads_enabled && !lease_valid_ && client.connected()) {
      client.disconnect();
    }
  }
}

const char* TuyaControl::relayName(Relay value) {
  switch (value) { case Relay::On: return "ON"; case Relay::Off: return "OFF"; default: return "UNKNOWN"; }
}
const char* TuyaControl::progressName(Progress value) {
  switch (value) {
    case Progress::Pending: return "pending"; case Progress::Sending: return "sending";
    case Progress::Verifying: return "verifying"; case Progress::Confirmed: return "confirmed";
    case Progress::Uncertain: return "uncertain"; case Progress::Failed: return "failed";
    default: return "idle";
  }
}
const char* TuyaControl::acceptName(Accept value) {
  switch (value) {
    case Accept::Accepted: return "accepted"; case Accept::Busy: return "busy";
    case Accept::Disabled: return "USB gate disabled"; case Accept::Offline: return "Wi-Fi offline";
    case Accept::Clock: return "NTP clock unavailable";
    case Accept::Storage: return "deadline storage unavailable";
    case Accept::LeaseActive: return "shutdown deadline already active";
    case Accept::StateUnknown: return "confirm OFF before ON";
  }
  return "invalid request";
}
#endif
