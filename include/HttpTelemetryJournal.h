#pragma once

#include "HttpTelemetryRecord.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace http_telemetry {

// Filesystem operations must flush before returning true. Existing files are
// never formatted or erased on mount failure. Segment names are numeric and
// belong exclusively to this journal instance.
class JournalStore {
public:
    virtual ~JournalStore() = default;
    virtual bool has_any() = 0;
    virtual bool exists(uint32_t segment) = 0;
    virtual bool size(uint32_t segment, size_t& bytes) = 0;
    virtual bool read(uint32_t segment, size_t offset, void* data, size_t bytes) = 0;
    virtual bool create(uint32_t segment) = 0;
    virtual bool append(uint32_t segment, const void* data, size_t bytes) = 0;
    virtual bool truncate(uint32_t segment, size_t bytes) = 0;
    virtual bool remove(uint32_t segment) = 0;
    virtual bool read_meta(unsigned slot, void* data, size_t bytes) = 0;
    virtual bool write_meta(unsigned slot, const void* data, size_t bytes) = 0;
};

class Journal {
public:
    enum class FullPolicy { reject, retain_recent };
    static constexpr size_t kSegmentRecords = 128;
    static constexpr uint32_t kMagic = 0x48575051; // HWPQ
    struct Meta {
        uint32_t magic;
        uint32_t generation;
        uint32_t head_segment;
        uint32_t head_index;
        uint32_t tail_segment;
        uint32_t crc;
    };
    static_assert(sizeof(Meta) == 24, "Journal metadata layout changed");

    Journal(JournalStore& store, uint64_t device_id, size_t max_pending,
            FullPolicy full_policy = FullPolicy::reject)
        : store_(store), device_id_(device_id), max_pending_(max_pending),
          full_policy_(full_policy) {}

    bool begin() {
        Meta a{}, b{};
        const bool va = store_.read_meta(0, &a, sizeof(a)) && meta_valid(a);
        const bool vb = store_.read_meta(1, &b, sizeof(b)) && meta_valid(b);
        if (!va && !vb) {
            // A pre-existing journal with lost metadata cannot be safely
            // interpreted as empty; retain it and fail closed.
            if (store_.has_any()) return false;
            if (!store_.create(1)) return false;
            meta_ = {kMagic, 1, 1, 0, 1, 0};
            seal_meta(meta_);
            if (!store_.write_meta(0, &meta_, sizeof(meta_))) return false;
            slot_ = 0;
        } else {
            slot_ = vb && (!va || b.generation > a.generation) ? 1 : 0;
            meta_ = slot_ ? b : a;
        }
        if (!meta_.head_segment || meta_.head_segment > meta_.tail_segment ||
            meta_.head_index > kSegmentRecords) return false;
        // The last checkpoint may precede a segment rollover. Find its
        // successor(s) without relying on a nondurable tail pointer.
        while (meta_.tail_segment < UINT32_MAX &&
               store_.exists(meta_.tail_segment + 1)) ++meta_.tail_segment;
        size_t total = 0;
        for (uint32_t seg = meta_.head_segment; seg <= meta_.tail_segment; ++seg) {
            size_t bytes = 0;
            if (!store_.size(seg, bytes)) return false;
            const bool final = seg == meta_.tail_segment;
            if (bytes % sizeof(Record)) {
                if (!final || !store_.truncate(seg, bytes - bytes % sizeof(Record)))
                    return false;
                bytes -= bytes % sizeof(Record);
            }
            if (bytes / sizeof(Record) > kSegmentRecords) return false;
            for (size_t i = 0; i < bytes / sizeof(Record); ++i) {
                Record record{};
                if (!store_.read(seg, i * sizeof(Record), &record, sizeof(record)) ||
                    !valid(record, device_id_)) return false;
            }
            if (seg == meta_.head_segment) {
                if (meta_.head_index > bytes / sizeof(Record)) return false;
                total += bytes / sizeof(Record) - meta_.head_index;
            } else total += bytes / sizeof(Record);
            if (seg == UINT32_MAX) break;
        }
        pending_ = total;
        ready_ = true;
        return true;
    }

    bool append(const Record& record) {
        if (!ready_ || !valid(record, device_id_)) return false;
        if (pending_ >= max_pending_ &&
            (full_policy_ != FullPolicy::retain_recent || !trim_oldest_segment())) {
            ++overflow_; return false;
        }
        size_t bytes = 0;
        if (!store_.size(meta_.tail_segment, bytes) || bytes % sizeof(Record))
            return false;
        if (bytes / sizeof(Record) == kSegmentRecords) {
            if (meta_.tail_segment == UINT32_MAX ||
                !store_.create(meta_.tail_segment + 1)) return false;
            ++meta_.tail_segment;
        }
        // A failed write or fsync may still have changed the file.  Stop all
        // journal operations until a fresh recovery scan establishes its
        // contents; retrying in place could reuse an uncertain record ID.
        if (!store_.append(meta_.tail_segment, &record, sizeof(record))) {
            ready_ = false;
            return false;
        }
        ++pending_;
        return true;
    }

    size_t batch(Record* output, size_t capacity) {
        if (!ready_ || !output || !capacity) return 0;
        size_t count = 0;
        uint32_t seg = meta_.head_segment;
        size_t index = meta_.head_index;
        while (count < capacity && count < pending_ && seg <= meta_.tail_segment) {
            size_t bytes = 0;
            if (!store_.size(seg, bytes)) return 0;
            while (index < bytes / sizeof(Record) && count < capacity && count < pending_) {
                if (!store_.read(seg, index * sizeof(Record), &output[count],
                                 sizeof(Record))) return 0;
                ++index; ++count;
            }
            ++seg; index = 0;
        }
        return count;
    }

    bool contains(const Record& target, bool& found) {
        found = false;
        if (!ready_) return false;
        for (uint32_t seg = meta_.head_segment; seg <= meta_.tail_segment; ++seg) {
            size_t bytes = 0;
            if (!store_.size(seg, bytes)) return false;
            for (size_t index = 0; index < bytes / sizeof(Record); ++index) {
                Record current{};
                if (!store_.read(seg, index * sizeof(Record), &current,
                                 sizeof(current))) return false;
                if (same_id(current, target)) { found = true; return true; }
            }
            if (seg == UINT32_MAX) break;
        }
        return true;
    }
    bool contains_batch(const Record* targets, size_t count, bool* found,
                        bool* conflicts) {
        if (!ready_ || !targets || !found || !conflicts || count > kMaxBatch)
            return false;
        for (size_t i = 0; i < count; ++i) found[i] = conflicts[i] = false;
        for (uint32_t seg = meta_.head_segment; seg <= meta_.tail_segment; ++seg) {
            size_t bytes = 0;
            if (!store_.size(seg, bytes)) return false;
            for (size_t index = 0; index < bytes / sizeof(Record); ++index) {
                Record current{};
                if (!store_.read(seg, index * sizeof(Record), &current,
                                 sizeof(current))) return false;
                for (size_t i = 0; i < count; ++i)
                    if (same_id(current, targets[i])) {
                        found[i] = true;
                        if (memcmp(&current, &targets[i], sizeof(Record)) != 0)
                            conflicts[i] = true;
                    }
            }
            if (seg == UINT32_MAX) break;
        }
        return true;
    }

    bool latest(Record& out) {
        if (!ready_ || !pending_) return false;
        for (uint32_t seg = meta_.tail_segment;; --seg) {
            size_t bytes = 0;
            if (!store_.size(seg, bytes)) return false;
            if (bytes >= sizeof(Record))
                return store_.read(seg, bytes - sizeof(Record), &out, sizeof(out));
            if (seg == meta_.head_segment) return false;
        }
    }

    // Call only after validating the authenticated ACK against the exact
    // batch and after the S3 has confirmed durable acceptance.
    bool acknowledge(size_t count) {
        if (!ready_ || !count || count > pending_) return false;
        Meta next = meta_;
        size_t remaining = count;
        while (remaining) {
            size_t bytes = 0;
            if (!store_.size(next.head_segment, bytes)) return false;
            const size_t available = bytes / sizeof(Record) - next.head_index;
            const size_t step = available < remaining ? available : remaining;
            next.head_index += step;
            remaining -= step;
            if (next.head_index == bytes / sizeof(Record) &&
                next.head_segment < next.tail_segment) {
                ++next.head_segment;
                next.head_index = 0;
            }
        }
        if (next.generation == UINT32_MAX) return false;
        ++next.generation;
        seal_meta(next);
        const unsigned other = slot_ ^ 1U;
        // A failed checkpoint write can still have reached durable storage.
        // Do not write another checkpoint from the old in-memory position.
        if (!store_.write_meta(other, &next, sizeof(next))) {
            ready_ = false;
            return false;
        }
        const uint32_t old_head = meta_.head_segment;
        meta_ = next;
        slot_ = other;
        pending_ -= count;
        for (uint32_t seg = old_head; seg < meta_.head_segment; ++seg)
            if (!store_.remove(seg)) ++cleanup_errors_;
        return true;
    }

    size_t pending() const { return pending_; }
    uint32_t overflows() const { return overflow_; }
    uint32_t cleanup_errors() const { return cleanup_errors_; }
    bool ready() const { return ready_; }

private:
    // Checkpoint the new head before deleting the old segment. After a crash,
    // either metadata still references the intact old segment or the newer
    // checkpoint references the intact next segment.
    bool trim_oldest_segment() {
        if (!ready_ || meta_.head_segment >= meta_.tail_segment ||
            meta_.generation == UINT32_MAX) return false;
        size_t bytes = 0;
        if (!store_.size(meta_.head_segment, bytes) ||
            bytes % sizeof(Record) ||
            bytes / sizeof(Record) < meta_.head_index) return false;
        const size_t removed = bytes / sizeof(Record) - meta_.head_index;
        if (!removed || removed > pending_) return false;
        Meta next = meta_;
        ++next.head_segment;
        next.head_index = 0;
        ++next.generation;
        seal_meta(next);
        const unsigned other = slot_ ^ 1U;
        if (!store_.write_meta(other, &next, sizeof(next))) {
            ready_ = false;
            return false;
        }
        const uint32_t old_head = meta_.head_segment;
        meta_ = next;
        slot_ = other;
        pending_ -= removed;
        if (!store_.remove(old_head)) ++cleanup_errors_;
        return true;
    }
    static bool meta_valid(const Meta& m) {
        return m.magic == kMagic && m.generation &&
               m.crc == checksum(&m, offsetof(Meta, crc));
    }
    static void seal_meta(Meta& m) { m.crc = checksum(&m, offsetof(Meta, crc)); }
    JournalStore& store_;
    const uint64_t device_id_;
    const size_t max_pending_;
    const FullPolicy full_policy_;
    Meta meta_{};
    unsigned slot_ = 0;
    size_t pending_ = 0;
    uint32_t overflow_ = 0;
    uint32_t cleanup_errors_ = 0;
    bool ready_ = false;
};
} // namespace http_telemetry
