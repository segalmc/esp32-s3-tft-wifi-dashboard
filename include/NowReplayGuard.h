#pragma once
#include <cstdint>

namespace now_telemetry {
class ReplayGuard {
public:
    bool allow(uint64_t authorized_boot, uint64_t sender_boot,
               uint32_t uptime_ms) const {
        return authorized_boot && sender_boot == authorized_boot &&
               (!seen_ || sender_boot != boot_ ||
                static_cast<int32_t>(uptime_ms - uptime_ms_) > 0);
    }
    void accepted(uint64_t sender_boot, uint32_t uptime_ms) {
        boot_ = sender_boot; uptime_ms_ = uptime_ms; seen_ = true;
    }
private:
    uint64_t boot_ = 0;
    uint32_t uptime_ms_ = 0;
    bool seen_ = false;
};
} // namespace now_telemetry
