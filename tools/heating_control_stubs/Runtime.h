#pragma once
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>
using String = std::string;
inline uint32_t fakeMs = 1000;
inline time_t fakeEpoch = 1800000000;
inline time_t mock_time(time_t* out) { if (out) *out = fakeEpoch; return fakeEpoch; }
inline uint32_t millis() { return fakeMs; }
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
inline void vTaskDelay(uint32_t ms) { fakeMs += ms; }
using QueueHandle_t = void*;
inline QueueHandle_t xQueueCreate(unsigned, unsigned) { return reinterpret_cast<void*>(1); }
inline bool queueSendFails = false;
inline int xQueueSend(QueueHandle_t, const void*, unsigned) { return queueSendFails ? 0 : 1; }
inline int xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, void*) { return 1; }
struct StopRun {};
inline int iterations = 0;
inline int maxIterations = 1;
inline std::function<void(int)> iterationHook;
inline int xQueueReceive(QueueHandle_t, void*, unsigned) {
  if (iterations++ >= maxIterations) throw StopRun{};
  if (iterationHook) iterationHook(iterations);
  return 0;
}
struct SerialMock {
  template<class... T> void printf(const char*, T...) {}
  void println(const char*) {}
};
inline SerialMock Serial;
struct IPAddress {
  uint32_t value;
  IPAddress(int a=0, int b=0, int c=0, int d=0): value((a<<24)|(b<<16)|(c<<8)|d) {}
  void fromString(const char*) { value = 1; }
  bool operator!=(const IPAddress& b) const { return value != b.value; }
};
constexpr int WL_CONNECTED = 3;
struct WiFiMock {
  bool available = true;
  int status() { return available ? WL_CONNECTED : 0; }
  IPAddress localIP() { return available ? IPAddress(192,168,1,2) : IPAddress(); }
};
inline WiFiMock WiFi;
struct NatureNetworkManager {
  enum class State { Online, Offline };
  struct Snapshot { State state = State::Online; bool ap_active = false; bool clock_synced = true; } state;
  Snapshot snapshot() { return state; }
};
struct Preferences {
  inline static std::map<std::string,std::vector<uint8_t>> bytes;
  inline static bool failWrite = false, failRead = false, failRemove = false, ready = true;
  inline static std::string failReadKey;
  bool begin(const char*, bool) { return ready; }
  bool isKey(const char* key) { return bytes.count(key); }
  size_t getBytesLength(const char* key) { auto i=bytes.find(key); return i==bytes.end() ? 0 : i->second.size(); }
  size_t getBytes(const char* key, void* out, size_t n) {
    if (failRead || failReadKey==key || !bytes.count(key) || bytes[key].size()!=n) return 0;
    memcpy(out, bytes[key].data(), n); return n;
  }
  size_t putBytes(const char* key, const void* in, size_t n) {
    if (failWrite) return 0;
    const auto* data=static_cast<const uint8_t*>(in); bytes[key]={data,data+n}; return n;
  }
  size_t putUChar(const char* key, uint8_t value) { return putBytes(key,&value,1); }
  uint8_t getUChar(const char* key, uint8_t fallback) { return bytes.count(key) && bytes[key].size()==1 ? bytes[key][0] : fallback; }
  bool remove(const char* key) { if (failRemove) return false; bytes.erase(key); return true; }
};
struct TuyaLocalConfig {
  IPAddress host; const char* deviceId; const char* localKey;
  uint32_t commandResponseTimeoutMs, heartbeatIntervalMs, statusReconciliationMs;
  struct { int stateBoolean; int controlBoolean; } dps;
};
struct TuyaLocalClient {
  enum class Error { None, Connect, Timeout, IncompleteFrame, Authentication };
  struct Diagnostics {
    Error lastError = Error::None; int lastResponseType=0;
    uint32_t lastCommand=0, lastFrameLength=0, sessions=0, heartbeatFailures=0;
    uint32_t connectionErrors=0, authenticationErrors=0, invalidResponses=0;
    bool connected=true;
  };
  struct Callbacks { void (*onStatus)(void*,const String&)=nullptr; void (*onPush)(void*,const String&)=nullptr; void* context=nullptr; };
  inline static bool relay=false, readsFail=false, writesApply=true, ack=true, connects=true;
  inline static unsigned onWrites=0, offWrites=0, reads=0;
  inline static std::function<void()> readHook, setHook;
  bool live=true;
  bool begin(const TuyaLocalConfig&, const Callbacks&) { return true; }
  bool connected() { return live; }
  bool connect() { live=connects; return live; }
  void disconnect() { live=false; }
  void invalidate() { live=false; }
  bool readStatus(String& json) {
    ++reads; if (readHook) readHook();
    if (readsFail) return false;
    json = relay ? "{\"dps\":{\"1\":true}}" : "{\"dps\":{\"1\":false}}";
    return true;
  }
  bool setBooleanDps(const char*, bool on, uint32_t) {
    if (on) { ++onWrites; assert(Preferences::bytes.count("lease")); assert(Preferences::bytes.count("session")); }
    else ++offWrites;
    if (writesApply) relay=on;
    if (setHook) setHook();
    return ack;
  }
  void poll(bool) {}
  Diagnostics diagnostics() { Diagnostics d; d.connected=live; return d; }
  static const char* errorName(Error) { return "mock-error"; }
  static const char* responseTypeName(int) { return "mock"; }
};
