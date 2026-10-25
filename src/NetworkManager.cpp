#include "NetworkManager.h"
#include "NetworkPolicy.h"
#include "OtaPolicy.h"
#include "TuyaControl.h"

#if TFT_HARDWARE_ENABLED
#include <esp_system.h>
#include <esp_sntp.h>
#include <time.h>
#include <string.h>

namespace {
constexpr uint32_t kCredentialMagic = 0x4E475731;
constexpr uint32_t kConnectTimeoutMs = 20000;
constexpr uint32_t kManualPortalMs = 600000;
constexpr uint32_t kPortalCloseDelayMs = 5000;
constexpr uint32_t kScanCooldownMs = 10000;
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr char kHex[] = "0123456789abcdef";
const char kPortalHtml[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Nature Glass Wi-Fi</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;min-height:100vh;background:linear-gradient(155deg,#081a2c,#194057 55%,#b66543);color:#f6fbff;font:16px system-ui,sans-serif;display:grid;place-items:center;padding:20px}.card{width:min(100%,440px);padding:25px;border:1px solid #67cfc2aa;border-radius:20px;background:#09243bdc;box-shadow:0 16px 45px #0008}h1{margin:0 0 6px;font-size:1.7rem}p{line-height:1.45;color:#cee6e9}label{display:block;margin:18px 0 7px;font-weight:600}input,select,button{font:inherit;width:100%;padding:13px;border-radius:10px}input,select{background:#102f43;color:white;border:1px solid #7da5aa}button{margin-top:20px;background:#65d7c9;color:#09243b;border:0;font-weight:700}button:disabled{opacity:.6}small{display:block;margin-top:10px;color:#d9e6e7}#result{min-height:2.8em}</style><main class="card"><h1>Nature Glass</h1><p>Connect this display to your home Wi-Fi. Keep your phone on the NatureGlass-Setup network while it checks the connection.</p><form id="form"><label for="list">Nearby networks</label><select id="list"><option value="">Scanning...</option></select><label for="ssid">Network name</label><input id="ssid" name="ssid" maxlength="32" required autocomplete="off"><label for="password">Wi-Fi password</label><input id="password" name="password" type="password" minlength="8" maxlength="63" required autocomplete="off"><input name="token" type="hidden" value="{{TOKEN}}"><button id="save" type="submit">Connect</button></form><small>Use a WPA2/WPA3 personal network (2.4 GHz). Hidden networks can be entered manually. Open and enterprise networks are unsupported.</small><p id="result" role="status"></p></main><script>
const list=document.querySelector('#list'),ssid=document.querySelector('#ssid'),result=document.querySelector('#result'),save=document.querySelector('#save');
async function scan(){try{const r=await fetch('/scan',{cache:'no-store'}),j=await r.json();if(j.scanning){setTimeout(scan,1600);return}list.innerHTML='<option value="">Enter manually</option>';for(const n of j.networks){const o=document.createElement('option');o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.open?' - open / unsupported':'');o.disabled=n.open;list.append(o)}}catch(e){result.textContent='Scan unavailable. Enter the network name manually.'}}
list.onchange=()=>{if(list.value)ssid.value=list.value};
document.querySelector('#form').onsubmit=async e=>{e.preventDefault();save.disabled=true;result.textContent='Checking Wi-Fi connection...';try{const body=new URLSearchParams(new FormData(e.target));const r=await fetch('/save',{method:'POST',body,cache:'no-store'});const j=await r.json();if(!r.ok){result.textContent=j.message||'Unable to start';save.disabled=false;return}poll()}catch(e){result.textContent='Request failed. Reconnect to NatureGlass-Setup and retry.';save.disabled=false}};
async function poll(){try{const r=await fetch('/status',{cache:'no-store'}),j=await r.json();if(j.result==='connecting'){setTimeout(poll,1500)}else if(j.result==='success'){result.textContent='Connected. Setup will close shortly.'}else{result.textContent=j.message||'Connection failed. Check the password and retry.';save.disabled=false}}catch(e){result.textContent='Connection status unavailable. Check the display or reconnect to setup.';save.disabled=false}}
scan();</script></html>)HTML";
using network_policy::due;
void appendJsonString(String& output, const String& input) {
  output += '"';
  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(input[i]);
    if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
    else if (c < 0x20) { output += '?'; }
    else output += static_cast<char>(c);
  }
  output += '"';
}
}  // namespace

void NatureNetworkManager::begin() {
  commands_ = xQueueCreate(2, sizeof(Command));
  if (!commands_ || xTaskCreate(taskEntry, "nature-wifi", 8192, this, 1, nullptr) != pdPASS) {
    Serial.println("Wi-Fi: task allocation failed");
    setState(State::Error);
  }
}

void NatureNetworkManager::taskEntry(void* context) {
  static_cast<NatureNetworkManager*>(context)->run();
}

void NatureNetworkManager::setState(State state) {
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.state = state;
  portEXIT_CRITICAL(&snapshot_lock_);
}

NatureNetworkManager::Snapshot NatureNetworkManager::snapshot() const {
  portENTER_CRITICAL(&snapshot_lock_);
  const Snapshot result = snapshot_;
  portEXIT_CRITICAL(&snapshot_lock_);
  return result;
}

NatureNetworkManager::OtaSnapshot NatureNetworkManager::otaSnapshot() const {
  portENTER_CRITICAL(&snapshot_lock_);
  const OtaSnapshot result = ota_snapshot_;
  portEXIT_CRITICAL(&snapshot_lock_);
  return result;
}

void NatureNetworkManager::setOtaStatus(OtaPhase phase, uint8_t percent, uint8_t error) {
  portENTER_CRITICAL(&snapshot_lock_);
  ota_snapshot_ = {phase, percent, error, ota_snapshot_.event + 1};
  portEXIT_CRITICAL(&snapshot_lock_);
}

bool NatureNetworkManager::copyOtaPassword(char* output, size_t size) const {
  if (!output || size < sizeof(ota_password_)) return false;
  portENTER_CRITICAL(&snapshot_lock_);
  const bool available = ota_password_ready_;
  if (available) memcpy(output, ota_password_, sizeof(ota_password_));
  portEXIT_CRITICAL(&snapshot_lock_);
  return available;
}

bool NatureNetworkManager::takeOtaRevealRequest() {
  const bool result = ota_reveal_requested_;
  ota_reveal_requested_ = false;
  return result;
}

bool NatureNetworkManager::copySetupPassword(char* output, size_t size) const {
  if (!output || size < sizeof(ap_password_)) return false;
  portENTER_CRITICAL(&snapshot_lock_);
  const bool available = snapshot_.ap_active && ap_password_[0] != '\0';
  if (available) memcpy(output, ap_password_, sizeof(ap_password_));
  portEXIT_CRITICAL(&snapshot_lock_);
  return available;
}

bool NatureNetworkManager::takeRevealRequest() {
  const bool result = reveal_requested_;
  reveal_requested_ = false;
  return result;
}

bool NatureNetworkManager::loadStorage() {
  if (!preferences_.begin("ngwifi", false)) return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);  // Clear any separate credentials saved by older firmware.
  String secret = preferences_.getString("apkey", "");
  bool valid_secret = secret.length() == 16;
  for (size_t i = 0; valid_secret && i < secret.length(); ++i) {
    if (!strchr(kAlphabet, secret[i])) valid_secret = false;
  }
  if (!valid_secret) {
    secret = "";
    for (uint8_t i = 0; i < 16; ++i) secret += kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    if (preferences_.putString("apkey", secret) != 16) return false;
  }
  portENTER_CRITICAL(&snapshot_lock_);
  secret.toCharArray(ap_password_, sizeof(ap_password_));
  portEXIT_CRITICAL(&snapshot_lock_);
  secret = "";
  // A separate NVS key leaves the AP password and saved home credentials intact.
  // A damaged existing key disables OTA instead of silently rotating its secret.
  bool ota_password_valid = false;
  if (preferences_.isKey("otakey")) {
    if (preferences_.getBytesLength("otakey") == sizeof(ota_password_) &&
        preferences_.getBytes("otakey", ota_password_, sizeof(ota_password_)) ==
            sizeof(ota_password_) && ota_password_[20] == '\0') {
      ota_password_valid = true;
      for (uint8_t i = 0; i < 20; ++i) {
        if (!strchr(kAlphabet, ota_password_[i])) ota_password_valid = false;
      }
    }
  } else {
    for (uint8_t i = 0; i < 20; ++i) {
      ota_password_[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    }
    ota_password_[20] = '\0';
    ota_password_valid = preferences_.putBytes("otakey", ota_password_,
                                              sizeof(ota_password_)) == sizeof(ota_password_);
  }
  if (!ota_password_valid) {
    memset(ota_password_, 0, sizeof(ota_password_));
    Serial.println("OTA: password unavailable; OTA disabled");
  }
  portENTER_CRITICAL(&snapshot_lock_);
  ota_password_ready_ = ota_password_valid;
  portEXIT_CRITICAL(&snapshot_lock_);
  if (preferences_.getBytesLength("home") == sizeof(saved_)) {
    preferences_.getBytes("home", &saved_, sizeof(saved_));
    has_saved_ = validCredentials(saved_);
  }
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.initial_setup = !has_saved_;
  portEXIT_CRITICAL(&snapshot_lock_);
  return true;
}

void NatureNetworkManager::run() {
  if (!loadStorage()) {
    Serial.println("Wi-Fi: NVS unavailable; protected setup disabled");
    setState(State::Error);
    vTaskDelete(nullptr);
  }
  if (has_saved_) {
    offline_since_ms_ = millis();
    startConnection(offline_since_ms_);
  } else {
    startPortal();
    if (snapshot().ap_active) setState(State::Setup);
  }
  for (;;) {
    const uint32_t now = millis();
    processCommands();
    tick(now);
    serviceOta();
    if (snapshot().ap_active) {
      dns_.processNextRequest();
      const uint32_t started_us = micros();
      server_.handleClient();
      const uint32_t elapsed_us = micros() - started_us;
      if (elapsed_us > portal_max_us_) portal_max_us_ = elapsed_us;
      if (elapsed_us > 1000) ++portal_slow_calls_;
      if (due(now, portal_stats_started_ms_ + 30000U)) {
        Serial.printf("Portal performance: handle_max=%lu us calls_over_1ms=%lu heap=%lu B PSRAM=%lu B\n",
                      static_cast<unsigned long>(portal_max_us_),
                      static_cast<unsigned long>(portal_slow_calls_),
                      static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()));
        portal_stats_started_ms_ = now;
        portal_max_us_ = 0;
        portal_slow_calls_ = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void NatureNetworkManager::startConnection(uint32_t now) {
  stopOta();
  WiFi.mode(snapshot().ap_active ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(saved_.ssid, saved_.password);
  attempt_started_ms_ = now;
  setState(State::Connecting);
  Serial.println("Wi-Fi: connecting (SSID and password withheld)");
}

void NatureNetworkManager::startPortal() {
  if (snapshot().ap_active) return;
  stopOta();  // ArduinoOTA's UDP listener otherwise accepts AP clients too.
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                         IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP("NatureGlass-Setup", ap_password_, 1, false, 1)) {
    Serial.println("Wi-Fi: protected setup AP failed");
    next_ap_attempt_ms_ = millis() + 30000U;
    if (!has_saved_) setState(State::Error);
    return;
  }
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.ap_active = true;
  portEXIT_CRITICAL(&snapshot_lock_);
  if (!portal_configured_) configurePortal();
  dns_.start(53, "*", WiFi.softAPIP());
  server_.begin();
  portal_started_ms_ = millis();
  portal_stats_started_ms_ = portal_started_ms_;
  portal_closing_ = false;
  portal_result_ = "ready";
  Serial.println("Wi-Fi: protected setup AP active at 192.168.4.1");
}

void NatureNetworkManager::stopPortal() {
  if (!snapshot().ap_active) return;
  Serial.printf("Portal performance at close: handle_max=%lu us calls_over_1ms=%lu heap=%lu B PSRAM=%lu B\n",
                static_cast<unsigned long>(portal_max_us_),
                static_cast<unsigned long>(portal_slow_calls_),
                static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()));
  dns_.stop();
  server_.stop();
  WiFi.scanDelete();
  WiFi.softAPdisconnect(true);  // Disable AP mode; STA remains enabled.
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.ap_active = false;
  portEXIT_CRITICAL(&snapshot_lock_);
  WiFi.mode(WIFI_STA);
}

void NatureNetworkManager::scheduleRetry(uint32_t now) {
  stopOta();
  WiFi.disconnect(false, false);
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.clock_synced = false;
  portEXIT_CRITICAL(&snapshot_lock_);
  retry_at_ms_ = now + retry_delay_ms_;
  retry_delay_ms_ = network_policy::nextRetryDelay(retry_delay_ms_);
  setState(State::RetryWait);
}

void NatureNetworkManager::processCommands() {
  Command command;
  while (commands_ && xQueueReceive(commands_, &command, 0) == pdTRUE) {
    if (command == Command::Setup) {
      portal_manual_ = true;
      startPortal();
      if (!has_saved_) setState(State::Setup);
    } else if (command == Command::RotateOta) {
      rotateOtaPassword();
    }
  }
}

void NatureNetworkManager::tick(uint32_t now) {
  if (has_pending_) {
    if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
      if (network_policy::commitAfterPersistence(saved_, pending_, [this](const Credentials& value) {
            return preferences_.putBytes("home", &value, sizeof(value)) == sizeof(value);
          })) {
        has_saved_ = true;
        portal_result_ = "success";
        portal_closing_ = true;
        portal_close_at_ms_ = now + kPortalCloseDelayMs;
        retry_delay_ms_ = 1000;
        setState(State::Online);
        startClock();
        Serial.println("Wi-Fi: new network associated, DHCP complete, configuration saved");
      } else {
        portal_result_ = "storage";
        Serial.println("Wi-Fi: credential storage failed; previous configuration retained");
        if (has_saved_) startConnection(now);
        else WiFi.disconnect(false, false);
      }
      clearPending();
    } else if (due(now, attempt_started_ms_ + kConnectTimeoutMs)) {
      portal_result_ = "failed";
      Serial.println("Wi-Fi: candidate association or DHCP failed; previous configuration retained");
      clearPending();
      if (has_saved_) startConnection(now);
      else {
        WiFi.disconnect(false, false);
        setState(State::Setup);
      }
    }
    return;
  }
  if (portal_closing_ && due(now, portal_close_at_ms_)) {
    portal_closing_ = false;
    portal_manual_ = false;
    stopPortal();
  }
  if (portal_manual_ && snapshot().ap_active && snapshot().state == State::Online &&
      due(now, portal_started_ms_ + kManualPortalMs)) {
    portal_manual_ = false;
    stopPortal();
  }
  if (!has_saved_) {
    if (!snapshot().ap_active && snapshot().state == State::Error &&
        due(now, next_ap_attempt_ms_)) {
      startPortal();
      if (snapshot().ap_active) setState(State::Setup);
    }
    return;
  }
  const bool linked = WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
  const State state = snapshot().state;
  if (linked) {
    if (state != State::Online) {
      Serial.println("Wi-Fi: associated and DHCP complete");
      retry_delay_ms_ = 1000;
      setState(State::Online);
      startClock();
      if (snapshot().ap_active && !portal_manual_ && !portal_closing_) stopPortal();
    }
    checkClock(now);
    return;
  }
  if (state == State::Online) {
    offline_since_ms_ = now;
    scheduleRetry(now);
  } else if (state == State::Connecting && due(now, attempt_started_ms_ + kConnectTimeoutMs)) {
    Serial.println("Wi-Fi: association or DHCP timed out");
    scheduleRetry(now);
  } else if (state == State::RetryWait && due(now, retry_at_ms_)) {
    startConnection(now);
  }
  if (!snapshot().ap_active && due(now, next_ap_attempt_ms_) &&
      network_policy::recoveryDue(has_saved_, linked, now, offline_since_ms_)) {
    portal_manual_ = false;
    startPortal();
  }
}

void NatureNetworkManager::processSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serial_line_[serial_length_] = '\0';
      if (strcmp(serial_line_, "wifi setup") == 0) {
        const Command command = Command::Setup;
        if (commands_) xQueueSend(commands_, &command, 0);
      } else if (strcmp(serial_line_, "wifi show") == 0) {
        reveal_requested_ = true;
      } else if (strcmp(serial_line_, "ota show") == 0) {
        ota_reveal_requested_ = true;
      } else if (strcmp(serial_line_, "ota rotate") == 0) {
        const Command command = Command::RotateOta;
        if (!commands_ || xQueueSend(commands_, &command, 0) != pdTRUE) {
          Serial.println("OTA: rotation request busy; retry over USB");
        }
      } else if (strcmp(serial_line_, "wifi status") == 0) {
        const Snapshot current = snapshot();
        const bool associated = WiFi.status() == WL_CONNECTED;
        const IPAddress address = associated ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
        char ip[16] = "none";
        if (address != IPAddress(0, 0, 0, 0)) {
          snprintf(ip, sizeof(ip), "%u.%u.%u.%u", address[0], address[1],
                   address[2], address[3]);
        }
        char ssid[33] = "none";
        char rssi_text[16] = "none";
        if (associated) {
          const String connected_ssid = WiFi.SSID();
          if (connected_ssid.length()) {
            const size_t length = min(connected_ssid.length(), sizeof(ssid) - 1);
            for (size_t i = 0; i < length; ++i) {
              const unsigned char ch = static_cast<unsigned char>(connected_ssid[i]);
              ssid[i] = ch >= 0x20 && ch <= 0x7e ? ch : '?';
            }
            ssid[length] = '\0';
          }
          const int32_t rssi = WiFi.RSSI();
          if (rssi != 0) snprintf(rssi_text, sizeof(rssi_text), "%ld dBm", static_cast<long>(rssi));
        }
        Serial.printf("Wi-Fi: state=%u AP=%s clock=%s IP=%s SSID=%s RSSI=%s heap=%lu PSRAM=%lu\n",
                      static_cast<unsigned>(current.state), current.ap_active ? "on" : "off",
                      current.clock_synced ? "synced" : "pending", ip, ssid, rssi_text,
                      static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getFreePsram()));
      } else if (tuya_ && strncmp(serial_line_, "tuya", 4) == 0) {
        if (strcmp(serial_line_, "tuya help") == 0) {
          Serial.println("Tuya USB: tuya status | on | off | reconnect | diagnostics | help");
          Serial.println("Tuya monitor: tuya monitor arm | tuya monitor disarm (USB only; arm only after proxy stops)");
          Serial.println("Tuya gates: tuya enable reads (temporary) | tuya enable control (armed monitor only)");
        } else if (strcmp(serial_line_, "tuya monitor arm") == 0 ||
                   strcmp(serial_line_, "tuya monitor disarm") == 0) {
          const bool arm = strcmp(serial_line_, "tuya monitor arm") == 0;
          const auto answer = tuya_->request(arm ? TuyaControl::Request::ArmMonitor :
                                                  TuyaControl::Request::DisarmMonitor);
          Serial.printf("Tuya: monitor %s %s\n", arm ? "arm" : "disarm",
                        TuyaControl::acceptName(answer));
        } else if (strcmp(serial_line_, "tuya enable reads") == 0) {
          tuya_->enableReadsFromUsb();
          Serial.println("Tuya: read gate enabled for this boot");
        } else if (strcmp(serial_line_, "tuya enable control") == 0) {
          Serial.println(tuya_->enableControlFromUsb() ?
              "Tuya: control gate enabled for this boot" :
              "Tuya: control gate remains closed; arm monitor first");
        } else if (strcmp(serial_line_, "tuya diagnostics") == 0 ||
                   strcmp(serial_line_, "tuya status") == 0) {
          const auto state = tuya_->snapshot();
          Serial.printf("Tuya: relay=%s connected=%s progress=%s result=%s lease=%s read_gate=%s control_gate=%s\n",
                        TuyaControl::relayName(state.relay), state.connected ? "yes" : "no",
                        TuyaControl::progressName(state.progress), state.result,
                        state.lease_active ? "yes" : "no", state.reads_enabled ? "on" : "off",
                        state.control_enabled ? "on" : "off");
          if (strcmp(serial_line_, "tuya diagnostics") == 0)
          Serial.printf("Tuya diagnostics: failures=%lu connection_or_timeout=%lu authentication=%lu invalid_responses=%lu last_confirmed_ms=%lu command_id=%lu pushes=%lu sessions=%lu heartbeat_failures=%lu monitor_armed=%s reconcile=30s session=persistent protocol=3.4 cmd=0x%02lX frame_len=%u response=%s failure=%s\n",
                          static_cast<unsigned long>(state.failures),
                          static_cast<unsigned long>(state.connection_failures),
                          static_cast<unsigned long>(state.authentication_failures),
                          static_cast<unsigned long>(state.auth_or_parse_failures),
                          static_cast<unsigned long>(state.last_confirmed_ms),
                          static_cast<unsigned long>(state.command_id),
                          static_cast<unsigned long>(state.push_events),
                          static_cast<unsigned long>(state.reconnects),
                          static_cast<unsigned long>(state.heartbeat_failures),
                          state.monitor_armed ? "yes" : "no",
                          static_cast<unsigned long>(state.last_response_command),
                          static_cast<unsigned>(state.last_frame_length),
                          state.last_response_type, state.last_failure);
          else {
            const auto answer = tuya_->request(TuyaControl::Request::Status);
            Serial.printf("Tuya: status read %s\n", TuyaControl::acceptName(answer));
          }
        } else {
          TuyaControl::Request request;
          if (strcmp(serial_line_, "tuya on") == 0) request = TuyaControl::Request::On;
          else if (strcmp(serial_line_, "tuya off") == 0) request = TuyaControl::Request::Off;
          else if (strcmp(serial_line_, "tuya reconnect") == 0) request = TuyaControl::Request::Reconnect;
          else { Serial.println("Tuya: unknown command; use tuya help"); serial_length_ = 0; continue; }
          uint32_t id = 0;
          const auto answer = tuya_->request(request, &id);
          Serial.printf("Tuya: request %s (id=%lu); inspect tuya status for verification\n",
                        TuyaControl::acceptName(answer), static_cast<unsigned long>(id));
        }
      }
      serial_length_ = 0;
    } else if (serial_length_ < sizeof(serial_line_) - 1) {
      serial_line_[serial_length_++] = c;
    } else {
      serial_length_ = 0;
    }
  }
}

void NatureNetworkManager::update() { processSerial(); }

void NatureNetworkManager::rotateOtaPassword() {
  char replacement[sizeof(ota_password_)] = {};
  for (uint8_t i = 0; i < sizeof(replacement) - 1; ++i) {
    replacement[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
  }
  stopOta();
  const bool stored = preferences_.putBytes("otakey", replacement, sizeof(replacement)) ==
                      sizeof(replacement);
  memset(replacement, 0, sizeof(replacement));
  if (!stored) {
    Serial.println("OTA: rotation failed; previous password retained");
    return;  // The old secret still works when serviceOta restarts.
  }
  // ArduinoOTA caches the password digest; reboot to activate the new key.
  // Disable service first so a delayed restart cannot expose the old digest.
  portENTER_CRITICAL(&snapshot_lock_);
  ota_password_ready_ = false;
  memset(ota_password_, 0, sizeof(ota_password_));
  portEXIT_CRITICAL(&snapshot_lock_);
  Serial.println("OTA: password rotated; restarting to activate it");
  ESP.restart();
}

void NatureNetworkManager::stopOta() {
  if (!ota_started_) return;
  ArduinoOTA.end();
  ota_started_ = false;
  if (otaSnapshot().phase == OtaPhase::Uploading) {
    setOtaStatus(OtaPhase::Error, 0, static_cast<uint8_t>(OTA_RECEIVE_ERROR));
  }
  Serial.println("OTA: service stopped");
}

void NatureNetworkManager::serviceOta() {
  const Snapshot current = snapshot();
  const bool linked = WiFi.status() == WL_CONNECTED &&
                      WiFi.localIP() != IPAddress(0, 0, 0, 0);
  if (!ota_policy::mayServe(current.state == State::Online, current.ap_active,
                            linked, ota_password_ready_)) {
    stopOta();
    return;
  }
  if (!ota_started_) {
    // Keep ArduinoOTA's MAC-derived hostname unique across multiple displays.
    ArduinoOTA.setPassword(ota_password_);
    ArduinoOTA.onStart([this]() {
      setOtaStatus(OtaPhase::Uploading, 0, 0);
      Serial.println("OTA: upload started");
    });
    ArduinoOTA.onProgress([this](unsigned int received, unsigned int total) {
      const uint8_t percent = total ? static_cast<uint8_t>(min(100U, received * 100U / total)) : 0;
      setOtaStatus(OtaPhase::Uploading, percent, 0);
    });
    ArduinoOTA.onEnd([this]() {
      setOtaStatus(OtaPhase::Success, 100, 0);
      Serial.println("OTA: upload complete; rebooting");
    });
    ArduinoOTA.onError([this](ota_error_t error) {
      setOtaStatus(OtaPhase::Error, 0, static_cast<uint8_t>(error));
      Serial.printf("OTA: error code %u\n", static_cast<unsigned>(error));
    });
    ArduinoOTA.begin();
    ota_started_ = true;
    Serial.println("OTA: service requested on Wi-Fi STA");
  }
  ArduinoOTA.handle();
}

bool NatureNetworkManager::apRequest() {
  if (snapshot().ap_active && server_.client().localIP() == WiFi.softAPIP()) return true;
  server_.send(403, "text/plain", "Setup is only available on NatureGlass-Setup.");
  return false;
}

void NatureNetworkManager::configurePortal() {
  portal_configured_ = true;
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t byte = static_cast<uint8_t>(esp_random());
    csrf_token_[2 * i] = kHex[byte >> 4];
    csrf_token_[2 * i + 1] = kHex[byte & 0x0F];
  }
  csrf_token_[32] = '\0';
  server_.enableDelay(false);
  server_.on("/", HTTP_GET, [this] { handleRoot(); });
  server_.on("/scan", HTTP_GET, [this] { handleScan(); });
  server_.on("/save", HTTP_POST, [this] { handleSave(); });
  server_.on("/status", HTTP_GET, [this] { handleStatus(); });
  server_.onNotFound([this] {
    if (!apRequest()) return;
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(302, "text/plain", "Open http://192.168.4.1/");
  });
}

void NatureNetworkManager::handleRoot() {
  if (!apRequest()) return;
  String page = FPSTR(kPortalHtml);
  page.replace("{{TOKEN}}", csrf_token_);
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.send(200, "text/html; charset=utf-8", page);
}

void NatureNetworkManager::handleScan() {
  if (!apRequest()) return;
  const uint32_t now = millis();
  if (!scan_running_ && (scan_started_ms_ == 0 || due(now, scan_started_ms_ + kScanCooldownMs))) {
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    scan_started_ms_ = now;
    scan_running_ = true;
    scan_json_ = "{\"scanning\":true}";
  }
  if (scan_running_) {
    const int16_t count = WiFi.scanComplete();
    if (count >= 0) {
      scan_json_ = "{\"scanning\":false,\"networks\":[";
      for (int16_t i = 0; i < count && i < 16; ++i) {
        if (i) scan_json_ += ',';
        scan_json_ += "{\"ssid\":";
        appendJsonString(scan_json_, WiFi.SSID(i));
        scan_json_ += ",\"rssi\":";
        scan_json_ += WiFi.RSSI(i);
        scan_json_ += ",\"open\":";
        scan_json_ += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true}" : "false}";
      }
      scan_json_ += "]}";
      WiFi.scanDelete();
      scan_running_ = false;
    } else if (count == WIFI_SCAN_FAILED) {
      scan_json_ = "{\"scanning\":false,\"networks\":[]}";
      scan_running_ = false;
    }
  }
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "application/json", scan_json_);
}

bool NatureNetworkManager::validCredentials(const Credentials& value) const {
  if (value.magic != kCredentialMagic) return false;
  return network_policy::validHomeCredentials(value.ssid, sizeof(value.ssid),
                                              value.password, sizeof(value.password));
}

void NatureNetworkManager::handleSave() {
  if (!apRequest()) return;
  server_.sendHeader("Cache-Control", "no-store");
  const int length = server_.clientContentLength();
  if (length <= 0 || length > 256 || server_.args() != 3 ||
      server_.arg("token") != csrf_token_) {
    server_.send(400, "application/json", "{\"message\":\"Invalid or oversized request. Reload setup and retry.\"}");
    return;
  }
  if (has_pending_ || portal_closing_) {
    server_.send(409, "application/json", "{\"message\":\"Connection check is already running.\"}");
    return;
  }
  const String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  if (ssid.length() > 32 || password.length() > 63 ||
      strlen(ssid.c_str()) != ssid.length() ||
      strlen(password.c_str()) != password.length()) {
    server_.send(400, "application/json", "{\"message\":\"Network name or password is too long.\"}");
    return;
  }
  memset(&pending_, 0, sizeof(pending_));
  pending_.magic = kCredentialMagic;
  ssid.toCharArray(pending_.ssid, sizeof(pending_.ssid));
  password.toCharArray(pending_.password, sizeof(pending_.password));
  if (!validCredentials(pending_)) {
    clearPending();
    server_.send(400, "application/json", "{\"message\":\"Enter a valid network name and an 8-63 character password.\"}");
    return;
  }
  portal_manual_ = true;  // Keep the AP available if the candidate fails.
  portal_started_ms_ = millis();
  portal_result_ = "connecting";
  has_pending_ = true;
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(pending_.ssid, pending_.password);
  attempt_started_ms_ = millis();
  setState(State::Connecting);
  server_.send(202, "application/json", "{\"result\":\"connecting\"}");
}

void NatureNetworkManager::handleStatus() {
  if (!apRequest()) return;
  server_.sendHeader("Cache-Control", "no-store");
  if (strcmp(portal_result_, "failed") == 0) {
    server_.send(200, "application/json", "{\"result\":\"failed\",\"message\":\"Could not associate or obtain an IP address. Check the password and 2.4 GHz network.\"}");
  } else if (strcmp(portal_result_, "storage") == 0) {
    server_.send(200, "application/json", "{\"result\":\"storage\",\"message\":\"Connected, but saving credentials failed. Retry setup.\"}");
  } else {
    String body = "{\"result\":\"";
    body += portal_result_;
    body += "\"}";
    server_.send(200, "application/json", body);
  }
}

void NatureNetworkManager::clearPending() {
  memset(&pending_, 0, sizeof(pending_));
  has_pending_ = false;
}

void NatureNetworkManager::startClock() {
  configTzTime("IST-2IDT,M3.4.4/26,M10.5.0", "pool.ntp.org", "time.nist.gov");
  clock_started_ms_ = millis();
  clock_warning_sent_ = false;
  portENTER_CRITICAL(&snapshot_lock_);
  snapshot_.clock_synced = false;
  portEXIT_CRITICAL(&snapshot_lock_);
}

void NatureNetworkManager::checkClock(uint32_t now) {
  if (snapshot().clock_synced) return;
  if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && time(nullptr) > 1700000000) {
    portENTER_CRITICAL(&snapshot_lock_);
    snapshot_.clock_synced = true;
    portEXIT_CRITICAL(&snapshot_lock_);
    Serial.println("Wi-Fi: clock synchronized; Israel timezone active");
  } else if (!clock_warning_sent_ && due(now, clock_started_ms_ + 30000U)) {
    clock_warning_sent_ = true;
    Serial.println("Wi-Fi: LAN connected; clock still unsynchronized (internet or NTP may be unavailable)");
  }
}
#endif
