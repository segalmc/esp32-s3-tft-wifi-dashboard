#pragma once
#include <cstdint>
namespace http_telemetry {
constexpr uint32_t kDiscoveryTimeoutMs = 600;
constexpr uint32_t kHttpConnectTimeoutMs = 1500;
constexpr uint32_t kHttpReadTimeoutMs = 1500;
constexpr uint32_t kChallengeLifetimeMs = 5000;
class ChallengeGuard {
public:
    void issue(uint64_t nonce, uint32_t now_ms) {
        nonce_ = nonce;
        issued_ms_ = now_ms;
    }
    bool consume(uint64_t supplied, uint32_t now_ms) {
        const uint64_t expected = nonce_;
        nonce_ = 0; // Every attempt consumes the challenge, including failure.
        return expected && supplied == expected &&
               static_cast<uint32_t>(now_ms - issued_ms_) <= kChallengeLifetimeMs;
    }
private:
    uint64_t nonce_ = 0;
    uint32_t issued_ms_ = 0;
};
inline uint32_t next_backoff(uint32_t current, uint32_t maximum) {
    if (current >= maximum) return maximum;
    return current > maximum / 2 ? maximum : current * 2;
}
} // namespace http_telemetry
