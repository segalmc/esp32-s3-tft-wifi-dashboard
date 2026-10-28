#include "../include/HttpTelemetryJournal.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

using namespace http_telemetry;
struct MemoryStore final : JournalStore {
    std::map<uint32_t, std::vector<uint8_t>> files;
    std::vector<uint8_t> meta[2];
    bool fail_meta = false;
    bool fail_append = false;
    bool has_any() override { return !files.empty() || !meta[0].empty() || !meta[1].empty(); }
    bool exists(uint32_t seg) override { return files.count(seg); }
    bool size(uint32_t seg, size_t& n) override {
        if (!exists(seg)) return false;
        n = files[seg].size(); return true;
    }
    bool read(uint32_t seg, size_t pos, void* out, size_t n) override {
        if (!exists(seg) || pos + n > files[seg].size()) return false;
        memcpy(out, files[seg].data() + pos, n); return true;
    }
    bool create(uint32_t seg) override { return files.emplace(seg, std::vector<uint8_t>{}).second; }
    bool append(uint32_t seg, const void* data, size_t n) override {
        if (!exists(seg) || fail_append) return false;
        auto* p = static_cast<const uint8_t*>(data);
        files[seg].insert(files[seg].end(), p, p + n); return true;
    }
    bool truncate(uint32_t seg, size_t n) override {
        if (!exists(seg) || n > files[seg].size()) return false;
        files[seg].resize(n); return true;
    }
    bool remove(uint32_t seg) override { return files.erase(seg) == 1; }
    bool read_meta(unsigned slot, void* out, size_t n) override {
        if (meta[slot].size() != n) return false;
        memcpy(out, meta[slot].data(), n); return true;
    }
    bool write_meta(unsigned slot, const void* data, size_t n) override {
        if (fail_meta) { meta[slot] = {1, 2, 3}; return false; }
        auto* p = static_cast<const uint8_t*>(data);
        meta[slot].assign(p, p + n); return true;
    }
};
static Record make(uint32_t sequence) {
    Record r{};
    r.version = kVersion; r.device_id = 123; r.boot_nonce = 456;
    r.sequence = sequence; r.boiler_centi = 4000; r.solar_centi = 5000;
    seal(r); return r;
}
int main() {
    MemoryStore store;
    Journal journal(store, 123, 130);
    assert(journal.begin());
    for (uint32_t i = 1; i <= 130; ++i) assert(journal.append(make(i)));
    assert(journal.pending() == 130);
    assert(!journal.append(make(131)) && journal.overflows() == 1);
    Record batch[20]{};
    assert(journal.batch(batch, 20) == 20 && batch[0].sequence == 1 && batch[19].sequence == 20);
    assert(journal.acknowledge(20));
    Journal restarted(store, 123, 130);
    assert(restarted.begin() && restarted.pending() == 110);
    assert(restarted.batch(batch, 20) == 20 && batch[0].sequence == 21);
    store.fail_meta = true;
    assert(!restarted.acknowledge(20) && !restarted.ready());
    store.fail_meta = false;
    Journal after_lost_ack(store, 123, 130);
    assert(after_lost_ack.begin() && after_lost_ack.pending() == 110);
    assert(after_lost_ack.batch(batch, 20) == 20 && batch[0].sequence == 21);
    assert(after_lost_ack.acknowledge(108));
    assert(after_lost_ack.pending() == 2);
    assert(after_lost_ack.batch(batch, 20) == 2 && batch[0].sequence == 129);
    Journal after_cleanup(store, 123, 130);
    assert(after_cleanup.begin() && after_cleanup.pending() == 2);
    // A torn final append is discarded; a corrupted complete record fails
    // closed because its durability cannot be inferred.
    store.files[2].push_back(0xee);
    Journal torn(store, 123, 130);
    assert(torn.begin() && torn.pending() == 2 && store.files[2].size() == 2 * sizeof(Record));
    store.files[2][0] ^= 1;
    Journal corrupt(store, 123, 130);
    assert(!corrupt.begin());
    // Lost metadata with surviving data must never become an empty queue.
    store.meta[0].clear(); store.meta[1].clear();
    Journal no_checkpoint(store, 123, 130);
    assert(!no_checkpoint.begin());

    MemoryStore uncertain_store;
    Journal uncertain(uncertain_store, 123, 10);
    assert(uncertain.begin());
    uncertain_store.fail_append = true;
    assert(!uncertain.append(make(1)) && !uncertain.ready());
    uncertain_store.fail_append = false;
    assert(!uncertain.append(make(1)));
    Journal recovered(uncertain_store, 123, 10);
    assert(recovered.begin() && recovered.append(make(1)));
}
