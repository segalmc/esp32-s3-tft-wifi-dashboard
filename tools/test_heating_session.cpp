#include "HeatingSessionPolicy.h"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <cstdio>

struct Store {
  std::map<std::string, std::vector<unsigned char>> values;
  bool failWrite = false, failRead = false, corrupt = false;
  bool isKey(const char* k) { return values.count(k); }
  size_t getBytesLength(const char* k) { return values[k].size(); }
  size_t getBytes(const char* k, void* p, size_t n) {
    if (failRead || values[k].size() != n) return 0;
    memcpy(p, values[k].data(), n);
    return n;
  }
  size_t putBytes(const char* k, const void* p, size_t n) {
    if (failWrite) return 0;
    const auto* b = static_cast<const unsigned char*>(p);
    values[k] = std::vector<unsigned char>(b, b + n);
    if (corrupt) values[k][0] ^= 1;
    return n;
  }
};
int main() {
  using namespace heating_session;
  using namespace boiler_policy;
  const int64_t now = 1800000000;
  assert(kDefaultSeconds == 1800 && kDefaultTargetC == 50);
  uint32_t parsedSeconds = 0, parsedTarget = 0;
  assert(parseSettings("1800","50",parsedSeconds,parsedTarget));
  assert(parsedSeconds == 1800 && parsedTarget == 50);
  assert(parseSettings("3600","60",parsedSeconds,parsedTarget));
  for (auto malformed : {"", "0900", "900junk", "+900", "-900", " 900", "900 ", "900.0", "99999999999999999"})
    assert(!parseSettings(malformed,"50",parsedSeconds,parsedTarget));
  for (auto malformed : {"", "050", "50junk", "50.0", "0", "-50", "99999999999999999"})
    assert(!parseSettings("1800",malformed,parsedSeconds,parsedTarget));
  assert(!parseSettings(nullptr,"50",parsedSeconds,parsedTarget));
  for (auto seconds : {900U,1800U,2700U,3600U}) {
    assert(validDuration(seconds));
    assert(physicallyAllowed(seconds) == (seconds <= 1800));
    assert(!elapsed(100, 100 + seconds * 1000 - 1, seconds));
    assert(elapsed(100, 100 + seconds * 1000, seconds));
  }
  for (auto seconds : {0U,60U,1799U,3601U,UINT32_MAX}) assert(!validDuration(seconds));
  for (auto target : {40U,50U,60U}) assert(validTarget(target));
  for (auto target : {0U,39U,41U,70U,UINT32_MAX}) assert(!validTarget(target));
  assert(freshTemperature(true,50,100,15100));
  assert(!freshTemperature(true,50,100,15101));
  assert(!freshTemperature(false,50,100,100));
  assert(!freshTemperature(true,NAN,100,100));
  assert(!freshTemperature(true,INFINITY,100,100));
  assert(!freshTemperature(true,-127,100,100));
  assert(!freshTemperature(true,126,100,100));
  assert(freshTemperature(true,50,UINT32_MAX-99,100));
  assert(reached(50,true,50) && reached(50,true,80));
  assert(!reached(50,true,49.9) && !reached(50,false,99));
  assert(!reached(0,true,99)); // Legacy lease has no target; timer still applies.
  assert(stopReason(true,true) == Stop::Timer);
  assert(stopReason(false,true) == Stop::Target);
  assert(stopReason(false,false) == Stop::None);
  assert(elapsed(UINT32_MAX-99,900000,900));
  const Lease legacy = makeLease(now);
  Store storage;
  assert(saveLease(storage,legacy));
  const auto originalBytes = storage.values[boiler_config::kLeaseKey];
  assert(!saveLease(storage, makeLease(now+60))); // Concurrent/repeated ON cannot extend.
  Record record{};
  assert(!loadRecord(storage,record,legacy)); // Existing legacy record stays intact.
  assert(storage.values[boiler_config::kLeaseKey] == originalBytes);
  Lease reboot{};
  assert(loadLease(storage,reboot));
  assert(reboot.deadline == legacy.deadline);
  assert(due(reboot,now-1,true)); // Backward clock uncertainty fails closed.
  assert(due(reboot,now+1,false));
  assert(due(reboot,0,false));
  assert(!due(reboot,now+1799,true) && due(reboot,now+1800,true));
  for (auto duration : {900U,1800U}) {
    Store s;
    const Lease lease = makeLease(now,BoilerConfig{duration});
    const Record session = makeRecord(lease,60);
    s.failWrite = true;
    assert(!saveRecord(s,session,lease) && !s.isKey(boiler_config::kLeaseKey));
    s.failWrite = false; s.failRead = true;
    assert(!saveRecord(s,session,lease));
    s.failRead = false; s.corrupt = true;
    assert(!saveRecord(s,session,lease));
    s.corrupt = false;
    assert(saveRecord(s,session,lease));
    assert(saveLease(s,lease));
    assert(loadLease(s,reboot) && loadRecord(s,record,reboot));
    assert(record.target == 60 && record.duration == duration);
    assert(!saveRecord(s,makeRecord(makeLease(now+1),40),makeLease(now+1)));
    assert(!validRecord(record,makeLease(now+1)));
    Record damaged = record; damaged.target = 40;
    assert(!validRecord(damaged,reboot));
    assert(!due(reboot,now+duration-1,true));
    assert(due(reboot,now+duration,true));
  }
  assert(!validLease({1,now+3600,now}));
  assert(!validLease({2,now+3600,now}));
  assert(!validLease({99,now+1800,now}));
  puts("PASS defaults, presets validation, all duration models, 45/60 physical restriction, temperature freshness/invalid/disconnect, timer precedence, NTP uncertainty, legacy migration, no extension, reboot, NVS write/read/corruption");
}
