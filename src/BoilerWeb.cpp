#include "BoilerWeb.h"
#if TFT_HARDWARE_ENABLED
#include "BoilerWebPolicy.h"
#include "BoilerWebApi.h"
#include "LocalHttpTelemetry.h"
#include "BoilerPolicy.h"
#include "BoilerDashboardPage.h"
#include "ControllerLinkStatus.h"
#include <ArduinoJson.h>
#include "secrets.h"
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <string.h>
#include <time.h>

namespace {
const char kLoginHtml[] PROGMEM = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Boiler sign in</title><style>:root{color-scheme:dark}body{font:16px system-ui;background:#102538;color:#eef8f8;min-height:100vh;display:grid;place-items:center;margin:0;padding:16px}main{width:min(100%,420px);background:#1c394b;border:1px solid #5b8e96;border-radius:16px;padding:24px}input,button{width:100%;box-sizing:border-box;padding:13px;border-radius:8px;font:inherit;margin-top:12px}input{background:#112a3a;color:white;border:1px solid #7a9}button{background:#64d4bc;border:0;font-weight:bold}</style><main><h1>Boiler</h1><form method="post" action="/login"><label for="password">Local dashboard password</label><input id="password" name="password" type="password" autocomplete="current-password" required><input type="hidden" name="csrf" value="{{CSRF}}"><button>Sign in</button></form></main></html>)HTML";
const char* otaPhaseName(NatureNetworkManager::OtaPhase phase) {
  switch (phase) {
    case NatureNetworkManager::OtaPhase::Idle: return "idle";
    case NatureNetworkManager::OtaPhase::Uploading: return "uploading";
    case NatureNetworkManager::OtaPhase::Success: return "success";
    case NatureNetworkManager::OtaPhase::Error: return "error";
  }
  return "unavailable";
}
}  // namespace

void BoilerWeb::begin(NatureNetworkManager* network, TuyaControl* control,
                      LocalHttpTelemetry* telemetry) {
  network_ = network;
  control_ = control;
  telemetry_ = telemetry;
  randomHex(login_csrf_, 16);
  if (xTaskCreate(taskEntry, "boiler-web", 9216, this, 1, nullptr) != pdPASS)
    Serial.println("Boiler Web: task unavailable");
}
void BoilerWeb::taskEntry(void* context) { static_cast<BoilerWeb*>(context)->run(); }
void BoilerWeb::randomHex(char* output, size_t bytes) {
  constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < bytes; ++i) {
    uint8_t value = static_cast<uint8_t>(esp_random());
    output[i * 2] = digits[value >> 4];
    output[i * 2 + 1] = digits[value & 15];
  }
  output[bytes * 2] = 0;
}
bool BoilerWeb::available() const {
  const auto wifi = network_->snapshot();
  return wifi.state == NatureNetworkManager::State::Online && !wifi.ap_active &&
         WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}
void BoilerWeb::run() {
  for (;;) {
    const bool allowed = available();
    const IPAddress ip = allowed ? WiFi.localIP() : IPAddress();
    if (running_ && (!allowed || ip != bound_ip_)) {
      server_->stop();
      server_.reset();
      running_ = false;
    }
    if (!running_ && allowed) {
      // A short Wi-Fi outage must not force every client to sign in again.
      // Keep sessions only if the service returns on the same bound address.
      if (bound_ip_ != IPAddress() && ip != bound_ip_) sessions_.clear();
      bound_ip_ = ip;
      server_.reset(new WebServer(ip, 80));
      configure();
      server_->begin();
      running_ = true;
      Serial.println("Boiler Web: listening on STA IP only (HTTP)");
    }
    if (running_) server_->handleClient();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}
void BoilerWeb::configure() {
  const char* names[] = {"Cookie", "Origin", "X-CSRF-Token", "Host"};
  server_->collectHeaders(names, 4);
  server_->enableDelay(false);
  server_->on("/", HTTP_GET, [this] { root(); });
  server_->on("/login", HTTP_POST, [this] { login(); });
  server_->on("/logout", HTTP_POST, [this] { logout(); });
  server_->on("/api/status", HTTP_GET, [this] { status(); });
  server_->on("/api/command", HTTP_POST, [this] { command(); });
  server_->on("/api/v1/telemetry/challenge", HTTP_GET,
              [this] { telemetry_->challenge(*server_); });
  server_->on("/api/v1/telemetry", HTTP_POST,
              [this] { telemetry_->receive(*server_); },
              [this] { telemetry_->onRaw(*server_); });
  server_->onNotFound([this] { reject(404, "not found"); });
}
void BoilerWeb::headers() {
  server_->sendHeader("Cache-Control", "no-store, max-age=0");
  server_->sendHeader("X-Content-Type-Options", "nosniff");
  // no-referrer makes a same-origin HTML form POST send Origin: null in Chrome.
  // same-origin keeps the origin available for strict POST checks and sends no
  // referrer to other origins.
  server_->sendHeader("Referrer-Policy", boiler_web_policy::kReferrerPolicy);
  server_->sendHeader("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'");
}
void BoilerWeb::reject(int code, const char* message) {
  headers();
  server_->send(code, "application/json", String("{\"message\":\"") + message + "\"}");
}
bool BoilerWeb::sameOrigin() {
  if (!available() || server_->client().localIP() != bound_ip_) return false;
  const String host = bound_ip_.toString();
  const String supplied_host = server_->header("Host");
  const String origin = server_->header("Origin");
  return boiler_web_policy::originMatches(host.c_str(), supplied_host.c_str(),
                                          origin.c_str());
}
boiler_web_sessions::Session* BoilerWeb::currentSession() {
  if (!available() || server_->client().localIP() != bound_ip_) return nullptr;
  const String cookie = server_->header("Cookie");
  return sessions_.find(cookie.c_str(), millis());
}
bool BoilerWeb::authorized() { return currentSession() != nullptr; }
bool BoilerWeb::csrfValid() {
  const String submitted = server_->header("X-CSRF-Token");
  const bool origin = sameOrigin();
  auto* session = origin ? currentSession() : nullptr;
  return boiler_web_policy::csrfAuthorized(origin, session != nullptr,
      session && boiler_web_policy::equalToken(session->csrf, submitted.c_str(), 32));
}
void BoilerWeb::root() {
  if (!available() || server_->client().localIP() != bound_ip_) return reject(403, "unavailable");
  headers();
  if (auto* session = currentSession()) {
    String html = FPSTR(kDashboardHtml);
    html.replace("{{CSRF}}", session->csrf);
    server_->send(200, "text/html; charset=utf-8", html);
  } else {
    String html = FPSTR(kLoginHtml);
    html.replace("{{CSRF}}", login_csrf_);
    server_->send(200, "text/html; charset=utf-8", html);
  }
}
void BoilerWeb::login() {
  if (!boiler_web_policy::loginRequestAuthorized(sameOrigin(),
                                                 server_->arg("csrf") == login_csrf_))
    return reject(403, "forbidden");
  if (server_->clientContentLength() > 256) return reject(413, "request too large");
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - failed_since_) >= boiler_web_policy::kRateWindowMs) {
    failed_since_ = now;
    failed_attempts_ = 0;
  }
  if (!boiler_web_policy::loginAllowed(failed_attempts_, failed_since_, now)) return reject(429, "try later");
  const String password = server_->arg("password");
  uint8_t hash[32] = {};
  bool accepted = false;
  if (password.length() >= 24 && password.length() <= 128) {
    uint8_t input[16 + 128];
    memcpy(input, WEB_PASSWORD_SALT, 16);
    memcpy(input + 16, password.c_str(), password.length());
    if (mbedtls_sha256(input, 16 + password.length(), hash, 0) == 0)
      accepted = boiler_web_policy::constantEqual(hash, WEB_PASSWORD_HASH, sizeof(hash));
    memset(input, 0, sizeof(input));
  }
  memset(hash, 0, sizeof(hash));
  if (!accepted) {
    ++failed_attempts_;
    return reject(401, "invalid credentials");
  }
  failed_attempts_ = 0;
  auto* session = sessions_.allocate(now);
  randomHex(session->token, 32);
  randomHex(session->csrf, 16);
  headers();
  server_->sendHeader("Set-Cookie", String("boiler_sid=") + session->token + "; Path=/; HttpOnly; SameSite=Strict");
  server_->sendHeader("Location", "/", true);
  server_->send(303, "text/plain", "Signed in");
}
void BoilerWeb::logout() {
  if (!csrfValid()) return reject(403, "forbidden");
  sessions_.invalidate(currentSession());
  headers();
  server_->sendHeader("Set-Cookie", "boiler_sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  server_->send(200, "application/json", "{\"message\":\"signed out\"}");
}
void BoilerWeb::status() {
  if (!authorized()) return reject(401, "sign in required");
  const auto state = control_->snapshot();
  const auto wifi = network_->snapshot();
  const auto ota = network_->otaSnapshot();
  const uint32_t now = millis();
  const bool confirmed = state.relay != TuyaControl::Relay::Unknown && state.last_confirmed_ms != 0;
  const uint32_t age = confirmed ? static_cast<uint32_t>(now - state.last_confirmed_ms) : 0;
  const bool fresh = confirmed && state.connected && !boiler_policy::stale(state.last_confirmed_ms, now);
  const bool onFresh = boiler_policy::freshConfirmedOff(
      state.relay == TuyaControl::Relay::Off, state.connected,
      state.last_confirmed_ms, now);
  const bool busy = state.busy;
  const boiler_web_policy::Availability capability{
      available(), state.reads_enabled, state.control_enabled, state.connected,
      state.relay == TuyaControl::Relay::Off, onFresh, state.lease_active, busy,
      wifi.clock_synced && boiler_policy::validTime(time(nullptr)), state.storage_ready};
  const String ssid = WiFi.SSID();
  const String ip = WiFi.localIP().toString();
  boiler_web_api::StatusData data;
  data.relay = boiler_web_policy::displayRelay(confirmed,
      state.relay == TuyaControl::Relay::On, fresh);
  data.lastConfirmedRelay = TuyaControl::relayName(state.relay);
  data.connected = state.connected;
  data.fresh = fresh;
  data.hasLastUpdate = confirmed;
  data.lastConfirmedAgeMs = age;
  data.lastConfirmedAtUptimeMs = confirmed ? state.last_confirmed_ms : 0;
  data.monitorArmed = state.monitor_armed;
  data.readGate = state.reads_enabled;
  data.controlGate = state.control_enabled;
  data.leaseActive = state.lease_active;
  data.progress = TuyaControl::progressName(state.progress);
  data.result = state.result;
  data.commandId = state.command_id;
  data.busy = busy;
  data.availability = capability;
  data.sessions = state.reconnects;
  data.pushEvents = state.push_events;
  data.heartbeatFailures = state.heartbeat_failures;
  data.connectionErrors = state.connection_failures;
  data.authenticationErrors = state.authentication_failures;
  data.invalidResponses = state.auth_or_parse_failures;
  data.lastCommand = state.last_response_command;
  data.lastFrameLength = state.last_frame_length;
  data.lastResponseType = state.last_response_type;
  data.lastFailure = state.last_failure;
  data.reconcileIntervalMs = state.reconcile_interval_ms;
  data.ssid = ssid.c_str();
  data.ip = ip.c_str();
  data.rssiDbm = WiFi.RSSI();
  data.uptimeMs = now;
  data.freeHeapBytes = ESP.getFreeHeap();
  data.freePsramBytes = ESP.getFreePsram();
  data.totalPsramBytes = ESP.getPsramSize();
  data.clockSynced = wifi.clock_synced;
  data.otaPhase = otaPhaseName(ota.phase);
  data.otaPercent = ota.percent;
  data.otaError = ota.error;
  DynamicJsonDocument document(3072);
  if (!boiler_web_api::populateStatus(document, data))
    return reject(503, "status unavailable");
  const auto link_state = telemetry_->snapshot();
  const auto link = controller_link::state(link_state.ever_message,
      static_cast<uint8_t>(link_state.transport),
      static_cast<uint8_t>(link_state.sender_intent),
      now - link_state.communication_ms,
      link_state.ever_now ? now - link_state.now_communication_ms : UINT32_MAX,
      link_state.ever_http ? now - link_state.http_communication_ms : UINT32_MAX,
      now);
  document["controllerLink"] = controller_link::name(link);
  document["controllerLastReceived"] = link_state.transport ==
      LocalHttpTelemetry::LinkTransport::EspNow ? "ESP-NOW" :
      link_state.transport == LocalHttpTelemetry::LinkTransport::Http ? "HTTP" : "NONE";
  document["controllerSenderIntent"] = link_state.sender_intent ==
      LocalHttpTelemetry::LinkTransport::EspNow ? "ESP-NOW" :
      link_state.sender_intent == LocalHttpTelemetry::LinkTransport::Http ? "HTTP" : "NONE";
  original_telemetry_api::Input original_input;
  original_input.snapshot_ok = link_state.snapshot_ok;
  original_input.ever_message = link_state.ever_message;
  original_input.ever_measurement = link_state.ever_measurement;
  original_input.communication_age_ms = link_state.ever_message ?
      static_cast<uint32_t>(now - link_state.communication_ms) : UINT32_MAX;
  original_input.receive_age_ms = link_state.ever_measurement ?
      static_cast<uint32_t>(now - link_state.measurement_ms) : UINT32_MAX;
  original_input.sample_age_at_receive_ms = link_state.sample_age_at_receive_ms;
  original_input.boiler_c = link_state.boiler_c;
  original_input.solar_c = link_state.solar_c;
  original_input.flags = link_state.flags;
  original_input.valve = link_state.valve;
  original_input.faults = link_state.faults;
  original_input.record_version = link_state.record_version;
  const auto original_view = original_telemetry_api::view(original_input);
  document["controllerMeasurementAgeMs"] = original_view.has_age ?
      original_view.age_ms : 0;
  document["controllerHasMeasurement"] = link_state.ever_measurement;
  if (!boiler_web_api::populateOriginalTelemetry(document, original_input))
    return reject(503, "status unavailable");
  String body;
  body.reserve(1800);
  serializeJson(document, body);
  headers();
  server_->send(200, "application/json", body);
}
void BoilerWeb::command() {
  if (!csrfValid()) return reject(403, "forbidden");
  if (server_->clientContentLength() > 64) return reject(413, "request too large");
  if (server_->args() != 1 ||
      (!server_->hasArg("action") && !server_->hasArg("target")))
    return reject(400, "invalid action");
  const bool legacyTarget = server_->hasArg("target");
  const String actionText = server_->arg(legacyTarget ? "target" : "action");
  const auto action = boiler_web_policy::parseAction(actionText.c_str());
  if (legacyTarget && action != boiler_web_policy::Action::On &&
      action != boiler_web_policy::Action::Off)
    return reject(400, "invalid action");
  TuyaControl::Request request;
  switch (action) {
    case boiler_web_policy::Action::Refresh: request = TuyaControl::Request::Status; break;
    case boiler_web_policy::Action::Reconnect: request = TuyaControl::Request::Reconnect; break;
    case boiler_web_policy::Action::On: request = TuyaControl::Request::On; break;
    case boiler_web_policy::Action::Off: request = TuyaControl::Request::Off; break;
    default: return reject(400, "invalid action");
  }
  uint32_t id = 0;
  const auto accepted = control_->request(request, &id);
  if (accepted != TuyaControl::Accept::Accepted)
    return reject(accepted == TuyaControl::Accept::Busy ? 409 : 503, TuyaControl::acceptName(accepted));
  StaticJsonDocument<128> receipt;
  if (!boiler_web_api::populateCommandReceipt(receipt, action, id))
    return reject(503, "response unavailable");
  String body;
  serializeJson(receipt, body);
  headers();
  server_->send(202, "application/json", body);
}
#endif
