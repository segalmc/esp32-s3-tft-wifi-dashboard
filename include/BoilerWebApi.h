#pragma once
#include <ArduinoJson.h>
#include "BoilerWebPolicy.h"
#include "OriginalTelemetryApi.h"

namespace boiler_web_api {
struct StatusData {
  const char* relay = "UNKNOWN";
  const char* lastConfirmedRelay = "UNKNOWN";
  bool connected = false;
  bool fresh = false;
  bool hasLastUpdate = false;
  uint32_t lastConfirmedAgeMs = 0;
  uint32_t lastConfirmedAtUptimeMs = 0;
  bool monitorArmed = false;
  bool readGate = false;
  bool controlGate = false;
  bool leaseActive = false;
  const char* progress = "idle";
  const char* result = "idle";
  uint32_t commandId = 0;
  bool busy = false;
  boiler_web_policy::Availability availability{};
  uint32_t sessions = 0;
  uint32_t pushEvents = 0;
  uint32_t heartbeatFailures = 0;
  uint32_t connectionErrors = 0;
  uint32_t authenticationErrors = 0;
  uint32_t invalidResponses = 0;
  uint32_t lastCommand = 0;
  uint16_t lastFrameLength = 0;
  const char* lastResponseType = "none";
  const char* lastFailure = "none";
  uint32_t reconcileIntervalMs = 0;
  const char* ssid = "";
  const char* ip = "";
  int32_t rssiDbm = 0;
  uint32_t uptimeMs = 0;
  uint32_t freeHeapBytes = 0;
  uint32_t freePsramBytes = 0;
  uint32_t totalPsramBytes = 0;
  bool clockSynced = false;
  const char* otaPhase = "unavailable";
  uint8_t otaPercent = 0;
  uint8_t otaError = 0;
};

template <typename Document>
bool populateStatus(Document& document, const StatusData& data) {
  document["relay"] = data.relay;
  document["lastConfirmedRelay"] = data.lastConfirmedRelay;
  document["connected"] = data.connected;
  document["fresh"] = data.fresh;
  if (data.hasLastUpdate) document["lastConfirmedAgeMs"] = data.lastConfirmedAgeMs;
  else document["lastConfirmedAgeMs"] = nullptr;
  document["lastConfirmedAtUptimeMs"] = data.lastConfirmedAtUptimeMs;
  document["monitorArmed"] = data.monitorArmed;
  document["readGate"] = data.readGate;
  document["controlGate"] = data.controlGate;
  document["leaseActive"] = data.leaseActive;
  document["progress"] = data.progress;
  document["result"] = data.result;
  document["commandId"] = data.commandId;
  document["busy"] = data.busy;
  JsonObject actions = document.createNestedObject("available");
  actions["refresh"] = boiler_web_policy::canOffer(boiler_web_policy::Action::Refresh, data.availability);
  actions["reconnect"] = boiler_web_policy::canOffer(boiler_web_policy::Action::Reconnect, data.availability);
  actions["on"] = boiler_web_policy::canOffer(boiler_web_policy::Action::On, data.availability);
  actions["off"] = boiler_web_policy::canOffer(boiler_web_policy::Action::Off, data.availability);
  JsonObject diagnostics = document.createNestedObject("diagnostics");
  diagnostics["sessions"] = data.sessions;
  diagnostics["pushEvents"] = data.pushEvents;
  diagnostics["heartbeatFailures"] = data.heartbeatFailures;
  diagnostics["connectionErrors"] = data.connectionErrors;
  diagnostics["authenticationErrors"] = data.authenticationErrors;
  diagnostics["invalidResponses"] = data.invalidResponses;
  diagnostics["lastCommand"] = data.lastCommand;
  diagnostics["lastFrameLength"] = data.lastFrameLength;
  diagnostics["lastResponseType"] = data.lastResponseType;
  diagnostics["lastFailure"] = data.lastFailure;
  diagnostics["reconcileIntervalMs"] = data.reconcileIntervalMs;
  JsonObject device = document.createNestedObject("device");
  device["ssid"] = data.ssid;
  device["ip"] = data.ip;
  device["rssiDbm"] = data.rssiDbm;
  device["uptimeMs"] = data.uptimeMs;
  device["freeHeapBytes"] = data.freeHeapBytes;
  device["freePsramBytes"] = data.freePsramBytes;
  device["totalPsramBytes"] = data.totalPsramBytes;
  device["clockSynced"] = data.clockSynced;
  device["otaPhase"] = data.otaPhase;
  device["otaPercent"] = data.otaPercent;
  device["otaError"] = data.otaError;
  return !document.overflowed();
}
template <typename Document>
bool populateOriginalTelemetry(Document& document,
                               const original_telemetry_api::Input& input) {
  const auto telemetry = original_telemetry_api::view(input);
  JsonObject original = document.createNestedObject("original");
  original["source"] = "LED_BENCH";
  original["connected"] = telemetry.connected;
  original["fresh"] = telemetry.fresh;
  if (telemetry.has_age) original["ageMs"] = telemetry.age_ms;
  else original["ageMs"] = nullptr;
  if (telemetry.boiler_valid) original["boilerTemperatureC"] = telemetry.boiler_c;
  else original["boilerTemperatureC"] = nullptr;
  if (telemetry.solar_valid) original["solarTemperatureC"] = telemetry.solar_c;
  else original["solarTemperatureC"] = nullptr;
  original["valveState"] = telemetry.valve_state;
  original["valveEvidence"] = telemetry.valve_evidence;
  original["valvePositionMeasured"] = false;
  original["mode"] = "UNKNOWN";
  original["targetTemperatureC"] = nullptr;
  if (telemetry.faults_known) {
    JsonObject faults = original.createNestedObject("faults");
    faults["role"] = telemetry.role_fault;
    faults["controller"] = telemetry.controller_fault;
    faults["sensorInvalid"] = telemetry.sensor_invalid;
    faults["interlock"] = telemetry.interlock;
  } else original["faults"] = nullptr;
  return !document.overflowed();
}
template <typename Document>
bool populateCommandReceipt(Document& document, boiler_web_policy::Action action,
                            uint32_t commandId) {
  if (action == boiler_web_policy::Action::Invalid || commandId == 0) return false;
  document["message"] = "queued for verification";
  document["action"] = boiler_web_policy::actionName(action);
  document["commandId"] = commandId;
  return !document.overflowed();
}
}  // namespace boiler_web_api
