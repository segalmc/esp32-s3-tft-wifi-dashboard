#include "../include/BoilerWebApi.h"
#include <cassert>
#include <cmath>
#include <cstring>
using namespace original_telemetry_api;

static StaticJsonDocument<3072> status(const Input& input) {
  StaticJsonDocument<3072> document;
  boiler_web_api::StatusData legacy;
  legacy.relay = "OFF";
  legacy.connected = true;
  assert(boiler_web_api::populateStatus(document, legacy));
  assert(boiler_web_api::populateOriginalTelemetry(document, input));
  return document;
}
int main() {
  Input input;
  auto old = status(input);
  assert(strcmp(old["relay"], "OFF") == 0);
  assert(old["connected"] == true);
  assert(old["available"].is<JsonObject>());
  assert(old["original"]["connected"] == false);
  assert(old["original"]["ageMs"].isNull());
  assert(old["original"]["boilerTemperatureC"].isNull());

  input.snapshot_ok = input.ever_message = input.ever_measurement = true;
  input.communication_age_ms = 1000;
  input.receive_age_ms = 1000;
  input.sample_age_at_receive_ms = 1000;
  input.flags = http_telemetry::kBoilerValid | http_telemetry::kSolarValid;
  input.boiler_c = 0.0f; input.solar_c = 62.5f;
  input.valve = static_cast<uint8_t>(http_telemetry::Valve::open_assumed);
  auto valid = status(input);
  assert(valid["original"]["connected"] == true);
  assert(valid["original"]["fresh"] == true);
  assert(valid["original"]["ageMs"] == 2000);
  assert(valid["original"]["boilerTemperatureC"] == 0.0f);
  assert(valid["original"]["solarTemperatureC"] == 62.5f);
  assert(strcmp(valid["original"]["valveState"], "OPEN") == 0);
  assert(strcmp(valid["original"]["valveEvidence"], "INFERRED_LED_BENCH") == 0);
  assert(valid["original"]["valvePositionMeasured"] == false);
  assert(strcmp(valid["original"]["mode"], "UNKNOWN") == 0);
  assert(valid["original"]["targetTemperatureC"].isNull());
  assert(valid["original"]["faults"]["role"] == false);

  input.flags = 0;
  auto missing = status(input);
  assert(missing["original"]["boilerTemperatureC"].isNull());
  assert(missing["original"]["solarTemperatureC"].isNull());
  assert(missing["original"]["faults"]["sensorInvalid"] == true);

  input.flags = http_telemetry::kBoilerValid | http_telemetry::kSolarValid;
  input.boiler_c = NAN; input.solar_c = 151.0f;
  auto invalid = status(input);
  assert(invalid["original"]["boilerTemperatureC"].isNull());
  assert(invalid["original"]["solarTemperatureC"].isNull());

  input.boiler_c = 55.0f; input.solar_c = 60.0f;
  input.receive_age_ms = 4000; input.sample_age_at_receive_ms = 4000;
  auto stale = status(input);
  assert(stale["original"]["connected"] == true);
  assert(stale["original"]["fresh"] == false);
  assert(stale["original"]["ageMs"] == 8000);
  assert(stale["original"]["boilerTemperatureC"].isNull());
  assert(strcmp(stale["original"]["valveState"], "UNKNOWN") == 0);
  assert(stale["original"]["faults"].isNull());

  input.communication_age_ms = 36000;
  auto disconnected = status(input);
  assert(disconnected["original"]["connected"] == false);
  assert(disconnected["original"]["fresh"] == false);

  input.communication_age_ms = 1000;
  input.receive_age_ms = 1000; input.sample_age_at_receive_ms = 0;
  input.valve = static_cast<uint8_t>(http_telemetry::Valve::unknown);
  auto unknown = status(input);
  assert(strcmp(unknown["original"]["valveState"], "UNKNOWN") == 0);
  assert(strcmp(unknown["original"]["valveEvidence"], "UNKNOWN") == 0);

  input.valve = static_cast<uint8_t>(http_telemetry::Valve::fault);
  input.faults = http_telemetry::kControllerFault;
  auto fault = status(input);
  assert(strcmp(fault["original"]["valveState"], "FAULT") == 0);
  assert(fault["original"]["faults"]["controller"] == true);
  assert(fault["original"]["faults"]["role"] == false);

  input.valve = static_cast<uint8_t>(http_telemetry::Valve::interlock);
  auto interlock = status(input);
  assert(strcmp(interlock["original"]["valveState"], "FAULT") == 0);
  assert(interlock["original"]["faults"]["interlock"] == true);

  assert(effective_age(UINT32_MAX, 2) == UINT32_MAX);
}
