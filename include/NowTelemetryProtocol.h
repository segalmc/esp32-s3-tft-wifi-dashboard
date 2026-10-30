#pragma once
#include "HttpTelemetryWire.h"

namespace now_telemetry {
using namespace http_telemetry;
constexpr uint8_t kDiscoveryFrame = 1;
constexpr uint8_t kDataFrame = 2;
constexpr uint8_t kAckFrame = 3;
constexpr uint16_t kIntentEspNow = 1;
constexpr uint16_t kIntentHttp = 2;
constexpr uint32_t kMagic = 0x31574f4e; // NOW1
constexpr uint8_t kMaxRecords = 3;
#pragma pack(push, 1)
struct DiscoveryFrame {
    uint32_t magic;
    uint8_t kind;
    uint8_t version;
    uint16_t reserved;
    uint64_t device_id;
    uint64_t sender_boot;
    uint64_t nonce;
    uint64_t receiver_boot;
    uint8_t mac[32];
};
struct DataPrefix {
    uint32_t magic;
    uint8_t kind;
    uint8_t version;
    uint16_t reserved;
    uint64_t receiver_boot;
    BatchHeader batch;
};
struct AckFrame {
    uint32_t magic;
    uint8_t kind;
    uint8_t version;
    uint16_t reserved;
    uint64_t receiver_boot;
    Ack ack;
};
#pragma pack(pop)
static_assert(sizeof(DiscoveryFrame) == 72, "discovery frame size");
static_assert(sizeof(DataPrefix) + kMaxRecords * sizeof(Record) + 32 <= 250,
              "ESP-NOW v1 payload must fit both radios");
static_assert(sizeof(AckFrame) <= 250, "ACK frame size");
inline size_t data_size(uint8_t count) {
    return sizeof(DataPrefix) + count * sizeof(Record) + 32;
}
inline bool discovery_valid(const DiscoveryFrame& frame, bool reply,
                            uint64_t device, uint64_t nonce = 0) {
    return frame.magic == kMagic && frame.version == kVersion &&
           frame.kind == kDiscoveryFrame && frame.reserved == 0 &&
           frame.device_id == device && frame.sender_boot && frame.nonce &&
           (reply ? frame.receiver_boot && frame.nonce == nonce :
                    frame.receiver_boot == 0);
}
inline bool data_valid(const DataPrefix& prefix, size_t length,
                       uint64_t device, uint64_t receiver_boot) {
    return prefix.magic == kMagic && prefix.kind == kDataFrame &&
           prefix.version == kVersion &&
           (prefix.reserved == kIntentEspNow || prefix.reserved == kIntentHttp) &&
           prefix.receiver_boot == receiver_boot && receiver_boot &&
           prefix.batch.count <= kMaxRecords &&
           valid_header(prefix.batch, device,
                        request_size(prefix.batch.count)) &&
           length == data_size(prefix.batch.count);
}
inline bool ack_valid(const AckFrame& frame, const BatchHeader& sent,
                      const Record* records, size_t count,
                      uint64_t receiver_boot) {
    return frame.magic == kMagic && frame.kind == kAckFrame &&
           frame.version == kVersion && frame.reserved == 0 &&
           frame.receiver_boot == receiver_boot &&
           ack_matches(frame.ack, sent, records, count);
}
} // namespace now_telemetry
