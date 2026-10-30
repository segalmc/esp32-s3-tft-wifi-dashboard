#pragma once
#include "HttpTelemetryRecord.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace http_telemetry {
constexpr uint32_t kBatchMagic = 0x31505448; // HTP1 on little-endian ESP32
constexpr uint32_t kDiscoveryMagic = 0x31444848; // HHD1
constexpr uint32_t kAckMagic = 0x314B4341; // ACK1
constexpr uint16_t kDiscoveryPort = 40173;
constexpr uint16_t kHttpPort = 80;
inline uint64_t device_id_from_uuid(const char* uuid) {
    if (!uuid || strlen(uuid) != 36) return 0;
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < 36; ++i) {
        const char c = uuid[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return 0;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}
constexpr uint8_t kBatchKindRecords = 1;
constexpr uint8_t kBatchKindHeartbeat = 2;
constexpr uint8_t kDiscoveryRequest = 1;
constexpr uint8_t kDiscoveryReply = 2;
#pragma pack(push, 1)
struct Discovery {
    uint32_t magic;
    uint8_t version;
    uint8_t kind;
    uint16_t port;
    uint64_t device_id;
    uint64_t boot_nonce;
    uint64_t request_nonce;
    uint8_t mac[32];
};
struct BatchHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t kind;
    uint8_t count;
    uint8_t health;
    uint64_t device_id;
    uint64_t sender_boot_nonce;
    uint64_t challenge_nonce;
    uint32_t sender_uptime_ms;
    uint64_t reference_boot_nonce;
    uint32_t reference_sequence;
};
struct Ack {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    uint64_t challenge_nonce;
    uint64_t last_boot_nonce;
    uint32_t last_sequence;
    uint8_t mac[32];
};
#pragma pack(pop)
static_assert(sizeof(Discovery) == 64, "Discovery layout changed");
static_assert(sizeof(BatchHeader) == 48, "Batch layout changed");
static_assert(sizeof(Ack) == 60, "ACK layout changed");
inline bool discovery_request_valid(const Discovery& request,
                                    uint64_t expected_device) {
    return request.magic == kDiscoveryMagic && request.version == kVersion &&
           request.kind == kDiscoveryRequest && request.port == 0 &&
           request.device_id == expected_device && request.boot_nonce &&
           request.request_nonce;
}
inline bool discovery_reply_valid(const Discovery& reply,
                                  uint64_t expected_device,
                                  uint64_t expected_request_nonce) {
    return reply.magic == kDiscoveryMagic && reply.version == kVersion &&
           reply.kind == kDiscoveryReply && reply.port == kHttpPort &&
           reply.device_id == expected_device && reply.boot_nonce &&
           reply.request_nonce == expected_request_nonce;
}
constexpr size_t kMaxRequest = sizeof(BatchHeader) + kMaxBatch * sizeof(Record) + 32;
inline size_t request_size(uint8_t count) {
    return sizeof(BatchHeader) + static_cast<size_t>(count) * sizeof(Record) + 32;
}
inline bool valid_header(const BatchHeader& h, uint64_t expected_device, size_t bytes) {
    return h.magic == kBatchMagic && h.version == kVersion &&
           h.device_id == expected_device && h.sender_boot_nonce &&
           h.challenge_nonce && h.count <= kMaxBatch &&
           ((h.kind == kBatchKindRecords && h.count > 0) ||
            (h.kind == kBatchKindHeartbeat && h.count == 0)) &&
           bytes == request_size(h.count);
}
inline bool ack_matches(const Ack& a, const BatchHeader& h,
                        const Record* sent, size_t sent_count) {
    if (a.magic != kAckMagic || a.version != kVersion ||
        a.challenge_nonce != h.challenge_nonce || a.count > sent_count ||
        a.reserved != 0) return false;
    if (h.kind == kBatchKindHeartbeat)
        return a.count == 0 && a.last_boot_nonce == h.reference_boot_nonce &&
               a.last_sequence == h.reference_sequence;
    return a.count > 0 && a.last_boot_nonce == sent[a.count - 1].boot_nonce &&
           a.last_sequence == sent[a.count - 1].sequence;
}
inline bool ack_hex_length_valid(size_t characters) {
    return characters == sizeof(Ack) * 2;
}
} // namespace http_telemetry
