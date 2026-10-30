#pragma once
#include "HttpTelemetryJournal.h"
#include "HttpTelemetryWire.h"
#include <cstring>

namespace http_telemetry {
struct Acceptance {
    uint8_t count = 0;
    uint8_t new_records = 0;
    uint8_t duplicates = 0;
    bool storage_error = false;
    bool identity_conflict = false;
    bool has_current = false;
    Record current{};
};
inline Acceptance accept_batch(Journal& journal, const BatchHeader& header,
                               const Record* records, uint64_t device_id) {
    Acceptance result;
    if (!records || header.kind != kBatchKindRecords ||
        header.count == 0 || header.count > kMaxBatch) return result;
    uint8_t valid_count = 0;
    for (; valid_count < header.count; ++valid_count)
        if (!valid(records[valid_count], device_id)) break;
    bool duplicates[kMaxBatch]{};
    bool conflicts[kMaxBatch]{};
    if (!journal.contains_batch(records, valid_count, duplicates, conflicts)) {
        result.storage_error = true; return result;
    }
    for (uint8_t index = 0; index < valid_count; ++index) {
        const Record& record = records[index];
        if (conflicts[index]) { result.identity_conflict = true; break; }
        bool duplicate = duplicates[index];
        for (uint8_t prior = 0; prior < index; ++prior) {
            if (prior >= result.count || !same_id(records[prior], record)) continue;
            if (memcmp(&records[prior], &record, sizeof(Record)) != 0) {
                result.identity_conflict = true; break;
            }
            duplicate = true;
        }
        if (result.identity_conflict) break;
        if (!duplicate && !journal.append(record)) {
            result.storage_error = true; break;
        }
        if (duplicate) ++result.duplicates; else ++result.new_records;
        if (record.boot_nonce == header.sender_boot_nonce &&
            record.uptime_ms <= header.sender_uptime_ms &&
            header.sender_uptime_ms - record.uptime_ms <= 30000 &&
            (!result.has_current || record.sequence > result.current.sequence)) {
            result.current = record;
            result.has_current = true;
        }
        ++result.count;
    }
    return result;
}
} // namespace http_telemetry
