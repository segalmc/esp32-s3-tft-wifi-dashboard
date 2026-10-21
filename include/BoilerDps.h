#pragma once
#include <ArduinoJson.h>

namespace boiler_dps {
enum class ParseError : uint8_t { None, Json, MissingOrInvalidDps1 };
inline bool parseRelay(const char* json, bool& on, ParseError* error = nullptr) {
  if (error) *error = ParseError::None;
  StaticJsonDocument<768> document;
  if (deserializeJson(document, json)) {
    if (error) *error = ParseError::Json;
    return false;
  }
  JsonVariant value = document["dps"]["1"];
  if (value.isNull()) value = document["data"]["dps"]["1"];
  if (!value.is<bool>()) {
    if (error) *error = ParseError::MissingOrInvalidDps1;
    return false;
  }
  on = value.as<bool>();
  return true;
}
}
