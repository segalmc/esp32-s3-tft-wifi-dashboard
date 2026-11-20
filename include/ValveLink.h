#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
namespace valve_link {
constexpr uint32_t magic = 0x31564c56; // VLV1; separate from unchanged telemetry
// Authenticated by the existing pairing key, bound to BOTH boot identities.
enum Action : uint8_t { status, manual, automatic, open, close, stop };
enum Result : uint8_t { none, accepted, rejected, completed, failed };
#pragma pack(push, 1)
struct Frame {
    uint32_t magic = valve_link::magic;
    uint8_t version = 1, kind = 0, action = 0, mode = 0;
    uint8_t target = 0, phase = 0, restoration = 0, allowed = 0, controller_reason = 0;
    uint32_t command_id = 0, sequence = 0;
    uint64_t device = 0, original_boot = 0, s3_boot = 0;
    uint32_t uptime_ms = 0, deadline_ms = 0;
    uint8_t result = 0;
    char reason[31]{};
    uint8_t mac[32]{};
};
#pragma pack(pop)
static_assert(sizeof(Frame) <= 250, "ESP-NOW payload limit");
inline bool valid(const Frame& f, uint8_t kind, uint64_t device,
                  uint64_t original_boot, uint64_t s3_boot) {
    return f.magic == magic && f.version == 1 && f.kind == kind &&
        f.device == device && f.original_boot == original_boot && original_boot &&
        f.s3_boot == s3_boot && s3_boot && f.action <= stop &&
        (kind != 2 || (f.mode >= 1 && f.sequence)) && f.mode <= 2 && f.target <= 2 && f.phase <= 6 && f.restoration <= 4 &&
        f.allowed <= 1 && f.controller_reason <= 14 && f.result <= failed && f.reason[sizeof(f.reason)-1] == 0;
}
class CommandIds {
public:
    void paired(uint64_t boot) {
        // A temporary telemetry disconnect does not reset replay protection.
        if (boot && boot != boot_) { boot_ = boot; highest_ = 0; }
    }
    bool admit(uint32_t id) {
        if (!id || id <= highest_) return false;
        highest_ = id; return true;
    }
private:
    uint64_t boot_ = 0; uint32_t highest_ = 0;
};
inline bool fresh_command(const Frame& f, uint32_t now) {
    const int32_t remaining = static_cast<int32_t>(f.deadline_ms - now);
    return f.command_id && remaining > 0 && remaining <= 5000;
}
inline const char* result_name(uint8_t r) {
    return r == accepted ? "ACCEPTED" : r == rejected ? "REJECTED" :
        r == completed ? "COMPLETED_INFERRED" : r == failed ? "FAILED" : "NONE";
}
inline const char* controller_reason_name(uint8_t reason) {
    static const char* names[] = {"BOOT","WAITING_FOR_SENSORS","INVALID_SENSOR_INPUT",
        "RESTORING","INTERLOCK","AUTO_OPEN","AUTO_CLOSE","RESTORATION_COMPLETE",
        "MOVEMENT_COMPLETE","STOP_OR_SENSOR_FAULT","TARGET_MISSING","TARGET_CORRUPT",
        "STORAGE_UNAVAILABLE","STORAGE_WRITE_FAILED","OUTPUT_FAULT"};
    return reason <= 14 ? names[reason] : "UNKNOWN";
}
inline const char* restoration_name(uint8_t r) {
    return r == 1 ? "WAITING_FOR_SENSORS" : r == 2 ? "MOVING" :
        r == 3 ? "COMPLETED_INFERRED" : r == 4 ? "FAILED" : "NO_SAVED_TARGET";
}
}
