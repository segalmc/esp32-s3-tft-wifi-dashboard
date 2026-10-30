#pragma once
#include <cmath>
#include <cstdint>
#include "HttpTelemetryRecord.h"

// This receiver is currently paired with the original board's LED bench
// sender. Its valve code describes inferred bench logic, not a sensed valve.
namespace original_telemetry_api {
struct Input {
  bool snapshot_ok = false;
  bool ever_message = false;
  bool ever_measurement = false;
  uint32_t communication_age_ms = UINT32_MAX;
  uint32_t receive_age_ms = UINT32_MAX;
  uint32_t sample_age_at_receive_ms = 0;
  float boiler_c = NAN;
  float solar_c = NAN;
  uint8_t flags = 0;
  uint8_t valve = 0;
  uint8_t faults = 0;
};
struct View {
  bool connected = false;
  bool fresh = false;
  bool has_age = false;
  uint32_t age_ms = 0;
  bool boiler_valid = false;
  bool solar_valid = false;
  float boiler_c = 0;
  float solar_c = 0;
  const char* valve_state = "UNKNOWN";
  const char* valve_evidence = "UNKNOWN";
  bool faults_known = false;
  bool role_fault = false;
  bool controller_fault = false;
  bool sensor_invalid = false;
  bool interlock = false;
};
inline uint32_t effective_age(uint32_t receive_age_ms,
                              uint32_t sample_age_at_receive_ms) {
  return receive_age_ms > UINT32_MAX - sample_age_at_receive_ms ? UINT32_MAX :
         receive_age_ms + sample_age_at_receive_ms;
}
inline bool temperature_valid(float value) {
  return std::isfinite(value) && value >= -50.0f && value <= 150.0f;
}
inline View view(const Input& input) {
  View result;
  result.connected = input.snapshot_ok && input.ever_message &&
                     input.communication_age_ms <= 35000U;
  result.has_age = input.snapshot_ok && input.ever_measurement;
  if (result.has_age)
    result.age_ms = effective_age(input.receive_age_ms,
                                  input.sample_age_at_receive_ms);
  result.fresh = result.connected && result.has_age &&
                 result.age_ms <= http_telemetry::kLiveMs;
  if (!result.fresh) return result;
  result.boiler_valid = (input.flags & http_telemetry::kBoilerValid) &&
                        temperature_valid(input.boiler_c);
  result.solar_valid = (input.flags & http_telemetry::kSolarValid) &&
                       temperature_valid(input.solar_c);
  if (result.boiler_valid) result.boiler_c = input.boiler_c;
  if (result.solar_valid) result.solar_c = input.solar_c;
  result.faults_known = true;
  result.role_fault = (input.faults & http_telemetry::kRoleFault) != 0;
  result.controller_fault = (input.faults & http_telemetry::kControllerFault) != 0;
  result.sensor_invalid = !result.boiler_valid || !result.solar_valid;
  result.interlock = input.valve == static_cast<uint8_t>(http_telemetry::Valve::interlock);
  switch (static_cast<http_telemetry::Valve>(input.valve)) {
    case http_telemetry::Valve::interlock: result.valve_state = "FAULT"; break;
    case http_telemetry::Valve::opening: result.valve_state = "OPENING"; break;
    case http_telemetry::Valve::closing: result.valve_state = "CLOSING"; break;
    case http_telemetry::Valve::open_assumed: result.valve_state = "OPEN"; break;
    case http_telemetry::Valve::closed_assumed: result.valve_state = "CLOSED"; break;
    case http_telemetry::Valve::fault: result.valve_state = "FAULT"; break;
    default: break;
  }
  if (input.valve >= static_cast<uint8_t>(http_telemetry::Valve::opening) &&
      input.valve <= static_cast<uint8_t>(http_telemetry::Valve::closed_assumed))
    result.valve_evidence = "INFERRED_LED_BENCH";
  return result;
}
} // namespace original_telemetry_api
