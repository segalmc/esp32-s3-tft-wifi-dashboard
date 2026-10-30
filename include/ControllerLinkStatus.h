#pragma once
#include <cstdint>

namespace controller_link {
enum class State : uint8_t { Connecting, EspNow, HttpFallback, Offline };
inline State state(bool ever, uint8_t received, uint8_t intended,
                   uint32_t age_ms, uint32_t now_age_ms, uint32_t http_age_ms,
                   uint32_t uptime_ms, uint32_t freshness_ms = 35000) {
    if (!ever) return uptime_ms <= freshness_ms ? State::Connecting : State::Offline;
    if (age_ms > freshness_ms) return State::Offline;
    if (received == 1 && intended == 1 && now_age_ms <= freshness_ms)
        return State::EspNow;
    if (intended == 2 && http_age_ms <= freshness_ms)
        return State::HttpFallback;
    return State::Connecting;
}
inline const char* name(State value) {
    switch (value) {
    case State::EspNow: return "ESP-NOW";
    case State::HttpFallback: return "HTTP FALLBACK";
    case State::Offline: return "OFFLINE";
    default: return "CONNECTING";
    }
}
} // namespace controller_link
