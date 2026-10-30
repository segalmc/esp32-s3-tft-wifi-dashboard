#pragma once
#include <cstdint>
namespace http_telemetry {
constexpr uint32_t kCommunicationLiveMs = 35000;
constexpr uint32_t kCommunicationOfflineMs = 75000;
enum class Communication : uint8_t { unknown, live, delayed, offline };
enum class Measurement : uint8_t { unknown, live, delayed, stale };
inline Communication communication(bool ever, uint32_t age_ms) {
    if (!ever) return Communication::unknown;
    if (age_ms <= kCommunicationLiveMs) return Communication::live;
    if (age_ms <= kCommunicationOfflineMs) return Communication::delayed;
    return Communication::offline;
}
inline Measurement measurement(bool ever, uint32_t age_ms) {
    if (!ever) return Measurement::unknown;
    if (age_ms <= kCommunicationLiveMs) return Measurement::live;
    if (age_ms <= kCommunicationOfflineMs) return Measurement::delayed;
    return Measurement::stale;
}
inline const char* name(Communication status) {
    switch (status) {
    case Communication::live: return "LIVE";
    case Communication::delayed: return "DELAYED";
    case Communication::offline: return "OFFLINE";
    default: return "UNKNOWN";
    }
}
inline const char* name(Measurement status) {
    switch (status) {
    case Measurement::live: return "LIVE";
    case Measurement::delayed: return "DELAYED";
    case Measurement::stale: return "STALE";
    default: return "UNKNOWN";
    }
}
} // namespace http_telemetry
