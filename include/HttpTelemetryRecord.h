#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Local HTTP telemetry v1. All fields are little-endian on the two ESP32
// targets. A persisted record is immutable; CRC covers every preceding byte.
namespace http_telemetry {
constexpr uint8_t kVersion = 1;
constexpr uint8_t kTargetRecordVersion = 2; // Same 42-byte layout; extended fault bits.
constexpr uint8_t kBoilerValid = 1;
constexpr uint8_t kSolarValid = 2;
constexpr uint8_t kClockValid = 4;
constexpr uint8_t kRoleFault = 1;
constexpr uint8_t kControllerFault = 2;
constexpr uint8_t kTargetMissing = 4;
constexpr uint8_t kTargetCorrupt = 8;
constexpr uint8_t kStorageUnavailable = 16;
constexpr uint8_t kTargetWriteFailed = 32;
constexpr uint8_t kRecoveryActive = 64;
constexpr uint8_t kStorageKnown = 128;
constexpr uint8_t kExtendedFaults = kTargetMissing | kTargetCorrupt |
    kStorageUnavailable | kTargetWriteFailed | kRecoveryActive | kStorageKnown;
enum class Valve : uint8_t {
    unknown = 0, interlock = 1, opening = 2, closing = 3,
    open_assumed = 4, closed_assumed = 5, fault = 6
};
constexpr size_t kMaxBatch = 20;
constexpr uint32_t kLiveMs = 7000;
constexpr uint32_t kDelayedMs = 15000;
constexpr uint32_t kStaleMs = 30000;

#pragma pack(push, 1)
struct Record {
    uint8_t version;
    uint8_t flags;
    uint8_t valve;
    uint8_t faults;
    uint64_t device_id;
    uint64_t boot_nonce;
    uint32_t sequence;
    uint32_t uptime_ms;
    int16_t boiler_centi;
    int16_t solar_centi;
    uint32_t epoch_s; // Zero when UTC has not been synchronized.
    uint16_t diagnostic;
    uint32_t crc;
};
#pragma pack(pop)
static_assert(sizeof(Record) == 42, "Telemetry record layout changed");

inline uint32_t checksum(const void* data, size_t length) {
    uint32_t crc = ~uint32_t{0};
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}
inline void seal(Record& record) {
    record.crc = checksum(&record, offsetof(Record, crc));
}
inline bool valid(const Record& record, uint64_t expected_device) {
    if ((record.version != kVersion && record.version != kTargetRecordVersion) ||
        record.device_id != expected_device ||
        !record.device_id || !record.boot_nonce || !record.sequence ||
        (record.flags & ~uint8_t{kBoilerValid | kSolarValid | kClockValid}) ||
        ((record.flags & kClockValid) ? record.epoch_s < 1700000000U
                                      : record.epoch_s != 0) ||
        record.valve > static_cast<uint8_t>(Valve::fault) ||
        (record.faults & ~static_cast<uint8_t>(kRoleFault | kControllerFault |
            (record.version == kTargetRecordVersion ? kExtendedFaults : 0))) ||
        (record.flags & kBoilerValid &&
         (record.boiler_centi < -5000 || record.boiler_centi > 15000)) ||
        (record.flags & kSolarValid &&
         (record.solar_centi < -5000 || record.solar_centi > 15000))) return false;
    return record.crc == checksum(&record, offsetof(Record, crc));
}
inline bool same_id(const Record& a, const Record& b) {
    return a.device_id == b.device_id && a.boot_nonce == b.boot_nonce &&
           a.sequence == b.sequence;
}
enum class Freshness : uint8_t { unknown, live, delayed, stale, offline };
inline Freshness freshness(bool ever, uint32_t age_ms, bool generation_trusted) {
    if (!ever) return Freshness::unknown;
    if (!generation_trusted || age_ms > kStaleMs) return Freshness::offline;
    if (age_ms > kDelayedMs) return Freshness::stale;
    if (age_ms > kLiveMs) return Freshness::delayed;
    return Freshness::live;
}
// ACKs may only remove a contiguous prefix of the exact batch sent. A record
// identity mismatch or a skipped item leaves the whole queue unchanged.
inline bool ack_prefix(const Record* sent, size_t sent_count,
                       const Record* acknowledged, size_t ack_count) {
    if (!sent || !acknowledged || !sent_count || sent_count > kMaxBatch ||
        ack_count > sent_count) return false;
    for (size_t i = 0; i < ack_count; ++i)
        if (!same_id(sent[i], acknowledged[i])) return false;
    return true;
}
} // namespace http_telemetry
