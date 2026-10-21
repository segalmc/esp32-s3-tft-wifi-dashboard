#include "BoilerPolicy.h"
#include "BoilerDps.h"
#include "BoilerWebPolicy.h"
#include "BoilerWebApi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct MockNvs {
  boiler_policy::Lease data{};
  bool present = false;
  bool fail_write = false;
  bool monitor_present = false;
  uint8_t monitor_value = 0;
  size_t getBytesLength(const char*) { return present ? sizeof(data) : 0; }
  size_t getBytes(const char*, void* destination, size_t size) {
    if (!present || size != sizeof(data)) return 0;
    memcpy(destination, &data, size);
    return size;
  }
  size_t putBytes(const char*, const void* source, size_t size) {
    if (fail_write || size != sizeof(data)) return 0;
    memcpy(&data, source, size);
    present = true;
    return size;
  }
  size_t putUChar(const char*, uint8_t value) {
    if (fail_write) return 0;
    monitor_present = true;
    monitor_value = value;
    return 1;
  }
  uint8_t getUChar(const char*, uint8_t fallback) {
    return monitor_present ? monitor_value : fallback;
  }
  bool isKey(const char*) { return monitor_present; }
  bool remove(const char*) { monitor_present = false; return true; }
};

int main() {
  using namespace boiler_policy;
  const int64_t start = 1800000000;
  assert(!mayTurnOn(false, start, true));
  assert(!mayTurnOn(true, 0, true));
  assert(!mayTurnOn(true, start, false));
  assert(mayTurnOn(true, start, true));
  assert(!freshConfirmedOff(false, true, 100, 200));
  assert(!freshConfirmedOff(true, false, 100, 200));
  assert(!freshConfirmedOff(true, true, 0, 200));
  assert(freshConfirmedOff(true, true, 100, 200));
  assert(!freshConfirmedOff(true, true, 100, 30101));
  Lease lease{kLeaseVersion, start + kMaxOnSeconds, start};
  assert(validLease(lease));
  assert(!due(lease, start + 1799, true));
  assert(due(lease, start + 1800, true));
  assert(due(lease, start + 1, false));  // Reboot without valid NTP: recover OFF.
  assert(!validLease({2, start + kMaxOnSeconds, start}));
  assert(!validLease({kLeaseVersion, start + 3600, start}));
  MockNvs nvs;
  assert(!loadLease(nvs, lease));
  nvs.fail_write = true;
  assert(!saveLease(nvs, lease));
  nvs.fail_write = false;
  assert(saveLease(nvs, lease));
  Lease rebooted{};
  assert(loadLease(nvs, rebooted) && rebooted.deadline == lease.deadline);
  assert(due(rebooted, start + 1, false));
  assert(reconnectDelayMs(0) == 1000);
  assert(reconnectDelayMs(1) == 2000);
  assert(reconnectDelayMs(9) == 32000);
  assert(stale(0, 1000));
  assert(!stale(1000, 1000 + kStaleMs));
  assert(stale(1000, 1001 + kStaleMs));
  assert(!monitorArmed(nvs));
  nvs.fail_write = true;
  assert(!armMonitor(nvs));
  nvs.fail_write = false;
  assert(armMonitor(nvs));
  assert(monitorArmed(nvs));  // A later boot loads authorization.
  const auto rebootGates = bootGates(nvs, true);
  assert(rebootGates.reads && !rebootGates.control);
  const auto storageFailureGates = bootGates(nvs, false);
  assert(!storageFailureGates.reads && !storageFailureGates.control);
  assert(disarmMonitor(nvs));
  assert(!monitorArmed(nvs));
  const auto disarmedBootGates = bootGates(nvs, true);
  assert(!disarmedBootGates.reads && !disarmedBootGates.control);
  bool pending = false;
  assert(reserve(pending, 0));
  assert(!reserve(pending, 0));  // A simultaneous Web/Serial request is rejected.
  pending = false;
  assert(busy(true, 0));
  assert(busy(false, 2));
  assert(!busy(false, 4));
  assert(!verified(true, false, true));  // ACK alone cannot confirm ON.
  assert(!verified(true, true, false));
  assert(verified(true, true, true));
  assert(verified(false, true, false));

  bool on = false;
  assert(boiler_dps::parseRelay("{\"dps\":{\"1\":true}}", on) && on);
  assert(boiler_dps::parseRelay("{\"data\":{\"dps\":{\"1\":false}}}", on) && !on);
  assert(!boiler_dps::parseRelay("{\"dps\":{\"11\":true}}", on));
  assert(!boiler_dps::parseRelay("{\"dps\":{\"1\":1}}", on));
  assert(!boiler_dps::parseRelay("{\"dps\":{\"1\":true", on));
  boiler_dps::ParseError parse_error = boiler_dps::ParseError::None;
  assert(!boiler_dps::parseRelay("{\"dps\":{\"1\":true", on, &parse_error));
  assert(parse_error == boiler_dps::ParseError::Json);
  assert(!boiler_dps::parseRelay("{\"dps\":{\"11\":true}}", on, &parse_error));
  assert(parse_error == boiler_dps::ParseError::MissingOrInvalidDps1);

  using namespace boiler_web_policy;
  assert(strcmp(kReferrerPolicy, "same-origin") == 0);
  constexpr char host[] = "192.168.31.230";
  constexpr char origin[] = "http://192.168.31.230";
  constexpr char loginCsrf[] = "0123456789abcdef0123456789abcdef";
  constexpr char sessionCsrf[] = "abcdef0123456789abcdef0123456789";
  const bool sameOrigin = originMatches(host, host, origin);
  assert(sameOrigin);
  assert(originMatches(host, "192.168.31.230:80", "http://192.168.31.230:80"));
  assert(!originMatches(host, host, ""));                       // Missing Origin.
  assert(!originMatches(host, host, "null"));                   // Opaque Origin.
  assert(!originMatches(host, host, "http://192.168.31.99"));  // Foreign Origin.
  assert(!originMatches(host, "192.168.31.99", origin));       // Foreign Host.
  assert(!originMatches(host, host, "https://192.168.31.230"));
  assert(!originMatches(host, host, "http://192.168.31.230:8080"));
  assert(loginRequestAuthorized(sameOrigin, equalToken(loginCsrf, loginCsrf, 32)));
  assert(!loginRequestAuthorized(originMatches(host, host, "null"), true));
  assert(!loginRequestAuthorized(sameOrigin, equalToken(loginCsrf, "", 32)));
  assert(!loginRequestAuthorized(sameOrigin,
                                 equalToken(loginCsrf, "0123456789abcdef0123456789abcdee", 32)));
  const uint8_t expectedPasswordHash[] = {0x11, 0x22, 0x33, 0x44};
  const uint8_t validPasswordHash[] = {0x11, 0x22, 0x33, 0x44};
  const uint8_t invalidPasswordHash[] = {0x11, 0x22, 0x33, 0x45};
  assert(loginRequestAuthorized(sameOrigin, true) &&
         loginAllowed(0, 0, 1) &&
         constantEqual(validPasswordHash, expectedPasswordHash, sizeof(expectedPasswordHash)));
  assert(!constantEqual(invalidPasswordHash, expectedPasswordHash,
                        sizeof(expectedPasswordHash)));  // Invalid credentials cannot establish a session.
  assert(!sessionAlive(false, 100, 100, 101));
  assert(sessionAlive(true, 100, 100, 101));
  assert(!sessionAlive(true, 100, 100, 100 + kIdleMs));
  assert(!sessionAlive(true, 100, 100 + kAbsoluteMs - 1, 100 + kAbsoluteMs));
  assert(loginAllowed(4, 100, 200));
  assert(!loginAllowed(5, 100, 200));
  assert(loginAllowed(5, 100, 100 + kRateWindowMs));
  assert(!csrfAuthorized(false, true, true));
  assert(!csrfAuthorized(true, false, true));
  assert(!csrfAuthorized(true, true, false));
  assert(csrfAuthorized(true, true, true));
  const char* sid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  assert(cookieHasSession("x=y; boiler_sid=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef; z=w", sid));
  assert(!cookieHasSession("x=boiler_sid=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", sid));
  assert(!cookieHasSession("boiler_sid=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdee", sid));
  const bool validSession = sessionAlive(true, 100, 100, 101) &&
      cookieHasSession("boiler_sid=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", sid);
  assert(csrfAuthorized(sameOrigin, validSession,
                        equalToken(sessionCsrf, sessionCsrf, 32)));  // Command and logout.
  assert(!csrfAuthorized(originMatches(host, host, "null"), validSession, true));
  assert(!csrfAuthorized(originMatches(host, host, "http://192.168.31.99"), validSession, true));
  assert(!csrfAuthorized(sameOrigin, false, true));             // Unauthenticated command.
  assert(!csrfAuthorized(sameOrigin, validSession, false));      // Missing/invalid CSRF.
  assert(parseAction("refresh") == Action::Refresh);
  assert(parseAction("reconnect") == Action::Reconnect);
  assert(parseAction("on") == Action::On);
  assert(parseAction("off") == Action::Off);
  assert(parseAction("monitor arm") == Action::Invalid);
  assert(parseAction("enable reads") == Action::Invalid);
  assert(parseAction("enable control") == Action::Invalid);
  assert(parseAction("status;on") == Action::Invalid);
  assert(parseAction("") == Action::Invalid);
  Availability capabilities{true, true, true, true, true, true,
                            false, false, true, true};
  assert(canOffer(Action::Refresh, capabilities));
  assert(canOffer(Action::Reconnect, capabilities));
  assert(canOffer(Action::On, capabilities));
  assert(canOffer(Action::Off, capabilities));
  assert(!canOffer(Action::Invalid, capabilities));
  capabilities.busy = true;
  assert(!canOffer(Action::Refresh, capabilities) && !canOffer(Action::On, capabilities));
  capabilities.busy = false;
  capabilities.controlGate = false;
  assert(canOffer(Action::Refresh, capabilities) && canOffer(Action::Reconnect, capabilities));
  assert(!canOffer(Action::On, capabilities) && !canOffer(Action::Off, capabilities));
  capabilities.controlGate = true;
  capabilities.readGate = false;
  assert(!canOffer(Action::Refresh, capabilities) && !canOffer(Action::Off, capabilities));
  capabilities.readGate = true;
  capabilities.connected = false;
  assert(!canOffer(Action::On, capabilities) && canOffer(Action::Off, capabilities));
  capabilities.connected = true;
  capabilities.confirmedOff = false;
  assert(!canOffer(Action::On, capabilities));  // Unknown or confirmed ON is never OFF.
  capabilities.confirmedOff = true;
  capabilities.fresh = false;
  assert(!canOffer(Action::On, capabilities));  // Stale OFF is not fresh OFF.
  capabilities.fresh = true;
  capabilities.leaseActive = true;
  assert(!canOffer(Action::On, capabilities));
  capabilities.leaseActive = false;
  capabilities.clockSynced = false;
  assert(!canOffer(Action::On, capabilities));
  capabilities.clockSynced = true;
  capabilities.storageReady = false;
  assert(!canOffer(Action::On, capabilities));
  capabilities.storageReady = true;
  capabilities.online = false;
  assert(!canOffer(Action::Reconnect, capabilities) && !canOffer(Action::Off, capabilities));
  assert(strcmp(displayRelay(false, false, false), "UNKNOWN") == 0);
  assert(strcmp(displayRelay(true, false, false), "STALE") == 0);
  assert(strcmp(displayRelay(true, false, true), "OFF") == 0);
  assert(strcmp(displayRelay(true, true, true), "ON") == 0);
  boiler_web_api::StatusData api;
  api.relay = displayRelay(true, false, false);
  api.lastConfirmedRelay = "OFF";
  api.hasLastUpdate = true;
  api.lastConfirmedAgeMs = 50000;
  api.availability = capabilities;
  api.ssid = "Kitchen \"Wi-Fi\" <test>";
  api.ip = "192.168.31.230";
  api.result = "state did not verify";
  api.reconcileIntervalMs = kReconcileMs;
  StaticJsonDocument<2304> apiDocument;
  assert(boiler_web_api::populateStatus(apiDocument, api));
  char apiResponse[2048] = {};
  assert(serializeJson(apiDocument, apiResponse, sizeof(apiResponse)) < sizeof(apiResponse));
  StaticJsonDocument<2304> parsedResponse;
  assert(!deserializeJson(parsedResponse, apiResponse));
  assert(strcmp(parsedResponse["relay"], "STALE") == 0);
  assert(strcmp(parsedResponse["lastConfirmedRelay"], "OFF") == 0);
  assert(parsedResponse["lastConfirmedAgeMs"] == 50000);
  assert(strcmp(parsedResponse["device"]["ssid"], api.ssid) == 0);
  assert(parsedResponse["available"]["on"] == false);
  assert(parsedResponse["available"]["off"] == false);
  assert(parsedResponse["diagnostics"]["reconcileIntervalMs"] == kReconcileMs);
  assert(parsedResponse["device"]["clockSynced"] == false);
  assert(parsedResponse["device"]["otaPhase"] == "unavailable");
  assert(parsedResponse["localKey"].isNull() && parsedResponse["deviceId"].isNull());
  StaticJsonDocument<64> undersizedResponse;
  assert(!boiler_web_api::populateStatus(undersizedResponse, api));
  api.relay = displayRelay(false, false, false);
  api.lastConfirmedRelay = "UNKNOWN";
  api.hasLastUpdate = false;
  StaticJsonDocument<2304> unknownDocument;
  assert(boiler_web_api::populateStatus(unknownDocument, api));
  assert(unknownDocument["relay"] == "UNKNOWN");
  assert(unknownDocument["lastConfirmedAgeMs"].isNull());
  StaticJsonDocument<128> receipt;
  assert(boiler_web_api::populateCommandReceipt(receipt, Action::Refresh, 12));
  char receiptJson[128] = {};
  assert(serializeJson(receipt, receiptJson, sizeof(receiptJson)) < sizeof(receiptJson));
  StaticJsonDocument<128> parsedReceipt;
  assert(!deserializeJson(parsedReceipt, receiptJson));
  assert(parsedReceipt["action"] == "refresh" && parsedReceipt["commandId"] == 12);
  assert(parsedReceipt["message"] == "queued for verification");
  StaticJsonDocument<128> invalidReceipt;
  assert(!boiler_web_api::populateCommandReceipt(invalidReceipt, Action::Invalid, 12));
  assert(!boiler_web_api::populateCommandReceipt(invalidReceipt, Action::On, 0));
  assert(equalToken("0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdef", 32));
  assert(!equalToken("0123456789abcdef0123456789abcdef", "0123456789abcdef0123456789abcdee", 32));
  puts("Boiler policy, DPS, queue, deadline, and Web auth tests passed");
}
