#include "../include/HttpTelemetryAcceptance.h"
#include <cassert>
#include <cstring>
#include <map>
#include <vector>
using namespace http_telemetry;
struct MemoryStore final : JournalStore {
    std::map<uint32_t, std::vector<uint8_t>> segments;
    std::vector<uint8_t> metadata[2];
    bool fail_meta = false;
    bool has_any() override { return !segments.empty() || !metadata[0].empty() || !metadata[1].empty(); }
    bool exists(uint32_t s) override { return segments.count(s); }
    bool size(uint32_t s, size_t& n) override { if (!exists(s)) return false; n=segments[s].size(); return true; }
    bool read(uint32_t s, size_t p, void* out, size_t n) override {
        if (!exists(s) || p+n>segments[s].size()) return false;
        memcpy(out, segments[s].data()+p,n); return true;
    }
    bool create(uint32_t s) override { return segments.emplace(s,std::vector<uint8_t>{}).second; }
    bool append(uint32_t s,const void* in,size_t n) override {
        if (!exists(s)) return false;
        auto* p=static_cast<const uint8_t*>(in);
        segments[s].insert(segments[s].end(),p,p+n); return true;
    }
    bool truncate(uint32_t s,size_t n) override { if (!exists(s)) return false; segments[s].resize(n); return true; }
    bool remove(uint32_t s) override { return segments.erase(s)==1; }
    bool read_meta(unsigned s,void* out,size_t n) override {
        if (metadata[s].size()!=n) return false;
        memcpy(out,metadata[s].data(),n);
        return true;
    }
    bool write_meta(unsigned s,const void* in,size_t n) override {
        if (fail_meta) return false;
        auto* p=static_cast<const uint8_t*>(in); metadata[s].assign(p,p+n); return true;
    }
};
static Record record(uint32_t sequence, uint64_t boot=4, uint32_t uptime=1000) {
    Record r{}; r.version=kVersion; r.device_id=3; r.boot_nonce=boot;
    r.sequence=sequence; r.uptime_ms=uptime; r.flags=kBoilerValid|kSolarValid;
    r.boiler_centi=4000; r.solar_centi=5000; seal(r); return r;
}
int main() {
    MemoryStore store;
    Journal s3(store,3,3); assert(s3.begin());
    BatchHeader request{}; request.kind=kBatchKindRecords; request.count=2;
    request.sender_boot_nonce=4; request.sender_uptime_ms=1100;
    Record first[2]={record(1),record(2)};
    auto accepted=accept_batch(s3,request,first,3);
    assert(accepted.count==2 && accepted.new_records==2 && accepted.duplicates==0);
    assert(accepted.has_current && accepted.current.sequence==2 && s3.pending()==2);
    // Lost HTTP response: replay is ACKed, with no second durable append.
    accepted=accept_batch(s3,request,first,3);
    assert(accepted.count==2 && accepted.new_records==0 && accepted.duplicates==2);
    assert(s3.pending()==2);
    Journal restarted(store,3,3); assert(restarted.begin());
    accepted=accept_batch(restarted,request,first,3);
    assert(accepted.count==2 && accepted.duplicates==2 && restarted.pending()==2);
    Record conflicting=first[0]; conflicting.boiler_centi=4200; seal(conflicting);
    request.count=1;
    accepted=accept_batch(restarted,request,&conflicting,3);
    assert(accepted.count==0 && accepted.identity_conflict && restarted.pending()==2);
    Record old=record(3,4,1000); request.sender_uptime_ms=50000;
    accepted=accept_batch(restarted,request,&old,3);
    assert(accepted.count==1 && !accepted.has_current && restarted.pending()==3);
    Record new_boot=record(1,5,50100); request.sender_boot_nonce=5;
    request.sender_uptime_ms=50200;
    accepted=accept_batch(restarted,request,&new_boot,3);
    assert(accepted.count==0 && accepted.storage_error && restarted.pending()==3);
    MemoryStore retained_store;
    Journal retained(retained_store,3,130,Journal::FullPolicy::retain_recent);
    assert(retained.begin());
    for (uint32_t sequence=1; sequence<=131; ++sequence)
        assert(retained.append(record(sequence)));
    assert(retained.pending()==3); // First durable 128-record segment retired.
    bool present=false;
    assert(retained.contains(record(1),present) && !present);
    assert(retained.contains(record(131),present) && present);
    Journal retained_restart(retained_store,3,130,Journal::FullPolicy::retain_recent);
    assert(retained_restart.begin() && retained_restart.pending()==3);
    Record latest{};
    assert(retained_restart.latest(latest) && latest.sequence==131);
    MemoryStore failed_store;
    Journal failed_trim(failed_store,3,130,Journal::FullPolicy::retain_recent);
    assert(failed_trim.begin());
    for (uint32_t sequence=1; sequence<=130; ++sequence)
        assert(failed_trim.append(record(sequence)));
    failed_store.fail_meta=true;
    assert(!failed_trim.append(record(131)) && !failed_trim.ready());
    failed_store.fail_meta=false;
    Journal failed_restart(failed_store,3,130,Journal::FullPolicy::retain_recent);
    assert(failed_restart.begin() && failed_restart.pending()==130);
    return 0;
}
