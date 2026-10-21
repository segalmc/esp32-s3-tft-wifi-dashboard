// Host-side tests for LynkTuya.h.
//
// A MockClient plays the role of the Tuya device: tests preload the byte
// stream the "device" will send and inspect the frames the library writes.
// Device-side crypto (building handshake responses, decrypting captured
// control frames) is re-implemented here directly on top of mbedtls, so the
// library's output is checked against an independent implementation.
//
// Build & run:  make test   (requires: brew install mbedtls)

#include <Arduino.h>
#include <WiFi.h>

#include <vector>
#include <algorithm>
#include <functional>
#include <atomic>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"

SerialStub Serial;

#include <LynkTuya.h>
#include <TuyaLocalClient.h>
#include <BoilerDps.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) \
  do { \
    checks++; \
    if (!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

/*==================== Mock client ====================*/

class MockClient : public Client {
public:
  std::vector<uint8_t> rx;  // bytes the "device" will send
  size_t rxPos = 0;
  std::vector<uint8_t> tx;  // bytes the library wrote
  size_t chunkLimit = SIZE_MAX;  // max bytes served per read, to simulate TCP fragmentation
  bool isConn = false;
  bool allowConnect = true;
  uint16_t lastPort = 0;
  int connectCalls = 0;
  std::function<void(uint32_t)> onWriteCommand;

  int connect(IPAddress, uint16_t port) override {
    lastPort = port;
    ++connectCalls;
    if (!allowConnect) return 0;
    isConn = true;
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    tx.insert(tx.end(), buf, buf + size);
    if (onWriteCommand && size >= 16 && buf[0] == 0 && buf[1] == 0 &&
        buf[2] == 0x55 && buf[3] == 0xAA) {
      const uint32_t command = (uint32_t(buf[8]) << 24) | (uint32_t(buf[9]) << 16) |
                               (uint32_t(buf[10]) << 8) | uint32_t(buf[11]);
      onWriteCommand(command);
    }
    return size;
  }
  int available() override {
    size_t rem = rx.size() - rxPos;
    return (int)std::min(rem, chunkLimit);
  }
  int read(uint8_t* buf, size_t size) override {
    size_t n = std::min({rx.size() - rxPos, size, chunkLimit});
    if (n == 0) return -1;
    memcpy(buf, rx.data() + rxPos, n);
    rxPos += n;
    return (int)n;
  }
  void stop() override {
    isConn = false;
  }
  uint8_t connected() override {
    return isConn;
  }

  void preload(const std::vector<uint8_t>& frame) {
    rx.insert(rx.end(), frame.begin(), frame.end());
  }
};

/*==================== Independent device-side crypto ====================*/

static const uint8_t LOCAL_KEY[17] = "0123456789abcdef";
static const uint8_t LOCAL_NONCE[17] = "0123456789abcdef";  // fixed nonce used by the library
static const uint8_t DEVICE_NONCE[17] = "fedcba9876543210";

static void ecb(const uint8_t* key, uint8_t* buf, size_t len, int mode) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mode == MBEDTLS_AES_ENCRYPT) mbedtls_aes_setkey_enc(&aes, key, 128);
  else mbedtls_aes_setkey_dec(&aes, key, 128);
  for (size_t i = 0; i < len; i += 16)
    mbedtls_aes_crypt_ecb(&aes, mode, buf + i, buf + i);
  mbedtls_aes_free(&aes);
}

static void hmac256(const uint8_t* key, const uint8_t* data, size_t len, uint8_t* out32) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, key, 16);
  mbedtls_md_hmac_update(&ctx, data, len);
  mbedtls_md_hmac_finish(&ctx, out32);
  mbedtls_md_free(&ctx);
}

static void gcmEnc(const uint8_t* key, const uint8_t* plain, size_t len,
                   const uint8_t* iv, const uint8_t* aad, size_t aadLen,
                   uint8_t* ct, uint8_t* tag) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
  mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, len, iv, 12, aad, aadLen, plain, ct, 16, tag);
  mbedtls_gcm_free(&gcm);
}

static int gcmDec(const uint8_t* key, const uint8_t* ct, size_t len,
                  const uint8_t* iv, const uint8_t* aad, size_t aadLen,
                  const uint8_t* tag, uint8_t* plain) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
  int rc = mbedtls_gcm_auth_decrypt(&gcm, len, iv, 12, aad, aadLen, tag, 16, ct, plain);
  mbedtls_gcm_free(&gcm);
  return rc;
}

static size_t pkcs7(uint8_t* buf, size_t len) {
  uint8_t pad = 16 - (len % 16);
  for (uint8_t i = 0; i < pad; i++) buf[len + i] = pad;
  return len + pad;
}

static void putU32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((x >> 24) & 0xFF);
  v.push_back((x >> 16) & 0xFF);
  v.push_back((x >> 8) & 0xFF);
  v.push_back(x & 0xFF);
}

static uint32_t getU32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

// Expected session keys, computed independently from the library.
static void expectedSessionKeyV34(uint8_t out[16], const uint8_t* nonce = LOCAL_NONCE) {
  for (int i = 0; i < 16; i++) out[i] = nonce[i] ^ DEVICE_NONCE[i];
  ecb(LOCAL_KEY, out, 16, MBEDTLS_AES_ENCRYPT);
}

static void expectedSessionKeyV35(uint8_t out[16]) {
  uint8_t xorNonce[16];
  for (int i = 0; i < 16; i++) xorNonce[i] = LOCAL_NONCE[i] ^ DEVICE_NONCE[i];
  uint8_t tag[16];
  gcmEnc(LOCAL_KEY, xorNonce, 16, LOCAL_NONCE, nullptr, 0, out, tag);
}

/*==================== Device-side frame builders ====================*/

// v3.4 device frame: authenticated header, return code, payload, and suffix.
static std::vector<uint8_t> v34DeviceFrame(uint32_t seq, uint32_t cmd, const std::vector<uint8_t>& encPayload) {
  std::vector<uint8_t> f;
  f.insert(f.end(), {0x00, 0x00, 0x55, 0xAA});
  putU32(f, seq);
  putU32(f, cmd);
  putU32(f, 4 + (uint32_t)encPayload.size() + 32 + 4);
  putU32(f, 0);  // retcode
  f.insert(f.end(), encPayload.begin(), encPayload.end());
  uint8_t sessionKey[16], digest[32];
  expectedSessionKeyV34(sessionKey);
  hmac256(cmd == 0x04 ? LOCAL_KEY : sessionKey, f.data(), f.size(), digest);
  f.insert(f.end(), digest, digest + 32);
  f.insert(f.end(), {0x00, 0x00, 0xAA, 0x55});
  return f;
}

static std::vector<uint8_t> v34DeviceFrameNoReturn(uint32_t seq, uint32_t cmd,
                                                    const std::vector<uint8_t>& encPayload) {
  std::vector<uint8_t> f;
  f.insert(f.end(), {0x00, 0x00, 0x55, 0xAA});
  putU32(f, seq);
  putU32(f, cmd);
  putU32(f, (uint32_t)encPayload.size() + 32 + 4);
  f.insert(f.end(), encPayload.begin(), encPayload.end());
  uint8_t sessionKey[16], digest[32];
  expectedSessionKeyV34(sessionKey);
  hmac256(cmd == 0x04 ? LOCAL_KEY : sessionKey, f.data(), f.size(), digest);
  f.insert(f.end(), digest, digest + 32);
  f.insert(f.end(), {0x00, 0x00, 0xAA, 0x55});
  return f;
}

static std::vector<uint8_t> v34Cmd4NoReturn() {
  uint8_t payload[48];
  memcpy(payload, DEVICE_NONCE, 16);
  hmac256(LOCAL_KEY, LOCAL_NONCE, 16, payload + 16);
  ecb(LOCAL_KEY, payload, 48, MBEDTLS_AES_ENCRYPT);
  return v34DeviceFrameNoReturn(1, 0x04, std::vector<uint8_t>(payload, payload + 48));
}

static std::vector<uint8_t> v34StatusNoReturn(const char* json) {
  uint8_t sessionKey[16], plain[PAYLOAD_BUFFER_SIZE];
  expectedSessionKeyV34(sessionKey);
  size_t len = strlen(json);
  memcpy(plain, json, len);
  len = pkcs7(plain, len);
  ecb(sessionKey, plain, len, MBEDTLS_AES_ENCRYPT);
  return v34DeviceFrameNoReturn(3, 0x10, std::vector<uint8_t>(plain, plain + len));
}

static std::vector<uint8_t> v34Cmd4Response() {
  uint8_t payload[48];
  memcpy(payload, DEVICE_NONCE, 16);
  hmac256(LOCAL_KEY, LOCAL_NONCE, 16, payload + 16);
  ecb(LOCAL_KEY, payload, 48, MBEDTLS_AES_ENCRYPT);
  return v34DeviceFrame(1, 0x04, std::vector<uint8_t>(payload, payload + 48));
}

// A 48-byte nonce+HMAC body gains a full PKCS7 block; a version-prefixed
// 63-byte body gains one padding byte. Both produce a 64-byte ciphertext.
static std::vector<uint8_t> v34Cmd4Padded(bool retcode, bool versionHeader = false,
                                           bool badPadding = false, bool badInnerHmac = false,
                                           bool badVersion = false,
                                           const uint8_t* nonce = LOCAL_NONCE) {
  uint8_t payload[64] = {};
  const size_t offset = versionHeader ? 15 : 0;
  if (versionHeader) memcpy(payload, "3.4", 3);
  if (badVersion) payload[3] = 1;
  memcpy(payload + offset, DEVICE_NONCE, 16);
  hmac256(LOCAL_KEY, nonce, 16, payload + offset + 16);
  if (badInnerHmac) payload[offset + 16] ^= 1;
  const size_t len = pkcs7(payload, offset + 48);
  if (badPadding) payload[len - 1] = 0;
  ecb(LOCAL_KEY, payload, len, MBEDTLS_AES_ENCRYPT);
  const std::vector<uint8_t> encrypted(payload, payload + len);
  return retcode ? v34DeviceFrame(1, 0x04, encrypted)
                 : v34DeviceFrameNoReturn(1, 0x04, encrypted);
}

static std::vector<uint8_t> v34JsonFrame(uint32_t seq, uint32_t cmd, const char* json, const uint8_t* sessionKey, bool versionHeader) {
  uint8_t plain[PAYLOAD_BUFFER_SIZE];
  size_t len = 0;
  if (versionHeader) {
    memcpy(plain, "3.4", 3);
    memset(plain + 3, 0, 12);
    len = 15;
  }
  memcpy(plain + len, json, strlen(json));
  len = pkcs7(plain, len + strlen(json));
  ecb(sessionKey, plain, len, MBEDTLS_AES_ENCRYPT);
  return v34DeviceFrame(seq, cmd, std::vector<uint8_t>(plain, plain + len));
}

static std::vector<uint8_t> v34EmptyAck(uint32_t seq, uint32_t cmd) {
  return v34DeviceFrame(seq, cmd, {});
}

// v3.5 device frame: 00006699 | reserved(2) | seq | cmd | len | iv | ct | tag | 00009966
static std::vector<uint8_t> v35DeviceFrame(uint32_t seq, uint32_t cmd, const uint8_t* key, const std::vector<uint8_t>& plain) {
  std::vector<uint8_t> f;
  f.insert(f.end(), {0x00, 0x00, 0x66, 0x99});
  f.push_back(0x00);
  f.push_back(0x00);
  putU32(f, seq);
  putU32(f, cmd);
  putU32(f, 12 + (uint32_t)plain.size() + 16);
  const uint8_t* aad = f.data() + 4;  // reserved + seq + cmd + len

  uint8_t iv[12];
  memcpy(iv, "abcdefghijkl", 12);  // arbitrary device-chosen IV
  std::vector<uint8_t> ct(plain.size());
  uint8_t tag[16];
  gcmEnc(key, plain.data(), plain.size(), iv, aad, 14, ct.data(), tag);

  f.insert(f.end(), iv, iv + 12);
  f.insert(f.end(), ct.begin(), ct.end());
  f.insert(f.end(), tag, tag + 16);
  f.insert(f.end(), {0x00, 0x00, 0x99, 0x66});
  return f;
}

static std::vector<uint8_t> v35Cmd4Response() {
  std::vector<uint8_t> plain(52, 0x00);  // retcode(4) + nonce(16) + hmac(32)
  memcpy(plain.data() + 4, DEVICE_NONCE, 16);
  hmac256(LOCAL_KEY, LOCAL_NONCE, 16, plain.data() + 20);
  return v35DeviceFrame(1, 0x04, LOCAL_KEY, plain);
}

static std::vector<uint8_t> v35JsonFrame(uint32_t seq, uint32_t cmd, const char* json, const uint8_t* sessionKey, bool versionHeader) {
  std::vector<uint8_t> plain(4, 0x00);  // retcode
  if (versionHeader) {
    const char* vh = "3.5";
    plain.insert(plain.end(), vh, vh + 3);
    plain.insert(plain.end(), 12, 0x00);
  }
  plain.insert(plain.end(), json, json + strlen(json));
  return v35DeviceFrame(seq, cmd, sessionKey, plain);
}

static std::vector<uint8_t> v35EmptyAck(uint32_t seq, uint32_t cmd, const uint8_t* sessionKey) {
  return v35DeviceFrame(seq, cmd, sessionKey, std::vector<uint8_t>(4, 0x00));  // retcode only
}

/*==================== Captured-frame inspection helpers ====================*/

// Splits the library's tx stream into v3.4 frames.
static std::vector<std::vector<uint8_t>> splitV34Frames(const std::vector<uint8_t>& tx) {
  std::vector<std::vector<uint8_t>> frames;
  size_t pos = 0;
  while (pos + 16 <= tx.size()) {
    uint32_t len = getU32(tx.data() + pos + 12);
    size_t total = 16 + len;
    if (pos + total > tx.size()) break;
    frames.emplace_back(tx.begin() + pos, tx.begin() + pos + total);
    pos += total;
  }
  return frames;
}

static std::vector<std::vector<uint8_t>> splitV35Frames(const std::vector<uint8_t>& tx) {
  std::vector<std::vector<uint8_t>> frames;
  size_t pos = 0;
  while (pos + 18 <= tx.size()) {
    uint32_t len = getU32(tx.data() + pos + 14);
    size_t total = 18 + len + 4;
    if (pos + total > tx.size()) break;
    frames.emplace_back(tx.begin() + pos, tx.begin() + pos + total);
    pos += total;
  }
  return frames;
}

// Decrypts the JSON payload of a captured v3.4 session frame (cmd 0x0d/0x10).
static std::string decryptV34Frame(const std::vector<uint8_t>& frame, const uint8_t* sessionKey, bool checkHmac = true) {
  uint32_t len = getU32(frame.data() + 12);
  size_t payloadLen = len - 36;
  if (checkHmac) {
    uint8_t hmac[32];
    hmac256(sessionKey, frame.data(), 16 + payloadLen, hmac);
    if (memcmp(hmac, frame.data() + 16 + payloadLen, 32) != 0) return "<BAD HMAC>";
  }
  std::vector<uint8_t> payload(frame.begin() + 16, frame.begin() + 16 + payloadLen);
  ecb(sessionKey, payload.data(), payloadLen, MBEDTLS_AES_DECRYPT);
  uint8_t pad = payload[payloadLen - 1];
  size_t plainLen = (pad <= payloadLen) ? payloadLen - pad : payloadLen;
  return std::string(payload.begin(), payload.begin() + plainLen);
}

// Decrypts the payload of a captured v3.5 session frame; empty string on auth failure.
static std::string decryptV35Frame(const std::vector<uint8_t>& frame, const uint8_t* sessionKey) {
  uint32_t len = getU32(frame.data() + 14);
  size_t ctLen = len - 12 - 16;
  std::vector<uint8_t> plain(ctLen);
  int rc = gcmDec(sessionKey, frame.data() + 30, ctLen,
                  frame.data() + 18, frame.data() + 4, 14,
                  frame.data() + 30 + ctLen, plain.data());
  if (rc != 0) return "<BAD TAG>";
  return std::string(plain.begin(), plain.end());
}

/*==================== Push callback capture ====================*/

static std::vector<std::string> pushedJson;
static void onPush(void*, const String& json) {
  pushedJson.push_back(json.c_str());
}
struct ObservedRelay {
  bool known = false;
  bool on = false;
  unsigned updates = 0;
};
static void onVerifiedRelay(void* context, const String& json) {
  onPush(nullptr, json);
  bool on = false;
  if (!boiler_dps::parseRelay(json.c_str(), on)) return;
  auto& state = *static_cast<ObservedRelay*>(context);
  state.known = true;
  state.on = on;
  ++state.updates;
}

/*==================== Tests ====================*/

static void testV34() {
  printf("-- v3.4 --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);

  MockClient mock;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(192, 168, 1, 50), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(300);
  dev.onDpsUpdate(onPush);
  pushedJson.clear();

  // Scripted device responses, in the order the library will consume them.
  mock.preload(v34Cmd4Response());                                              // handshake
  mock.preload(v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":true}}", sessionKey, false));  // getStatus
  mock.preload(v34JsonFrame(4, 0x10, "{\"dps\":{\"11\":false,\"1\":true}}", sessionKey, false));  // getRelayState
  mock.preload(v34EmptyAck(5, 0x0d));                                           // turn()
  // push arrives before the next command ack:
  mock.preload(v34JsonFrame(6, 0x08, "{\"protocol\":4,\"t\":1,\"data\":{\"dps\":{\"1\":false}}}", sessionKey, true));
  mock.preload(v34EmptyAck(7, 0x0d));                                           // setDps int
  mock.preload(v34EmptyAck(8, 0x09));                                           // heartbeat
  mock.preload(v34JsonFrame(9, 0x08, "{\"data\":{\"dps\":{\"9\":42}}}", sessionKey, true));  // poll push

  // getStatus + JSON out
  String json;
  CHECK(dev.getStatus(json, false));
  CHECK(json == "{\"dps\":{\"1\":true}}");

  // getRelayState: "1" must not match inside "11"
  bool on = false;
  CHECK(dev.getRelayState(on, "1", false));
  CHECK(on == true);

  // turn on with explicit timestamp
  CHECK(dev.turn((uint32_t)1700000000, true, false));

  // setDps int; the push queued before the ack must reach the callback
  CHECK(dev.setDps("7", 42, false));
  CHECK(pushedJson.size() == 1);
  CHECK(pushedJson[0] == "{\"protocol\":4,\"t\":1,\"data\":{\"dps\":{\"1\":false}}}");

  // heartbeat
  CHECK(dev.heartbeat());

  // poll dispatches a pending push
  CHECK(dev.poll());
  CHECK(pushedJson.size() == 2);
  CHECK(pushedJson[1] == "{\"data\":{\"dps\":{\"9\":42}}}");

  // Oversized payload is rejected before anything is written
  size_t txBefore = mock.tx.size();
  std::string big(300, 'x');
  CHECK(dev.setDps("1", big.c_str(), false) == false);
  CHECK(mock.tx.size() == txBefore);

  // ---- Inspect what the library actually sent ----
  auto frames = splitV34Frames(mock.tx);
  CHECK(frames.size() == 7);  // cmd3, cmd5, status x2, control x2, heartbeat
  if (frames.size() >= 7) {
    // cmd3: payload decrypts (with the local key) back to the padded local nonce
    CHECK(getU32(frames[0].data() + 8) == 0x03);
    std::vector<uint8_t> nonce(frames[0].begin() + 16, frames[0].begin() + 48);
    ecb(LOCAL_KEY, nonce.data(), 32, MBEDTLS_AES_DECRYPT);
    CHECK(memcmp(nonce.data(), LOCAL_NONCE, 16) == 0);
    // cmd3 HMAC is keyed with the local key
    uint8_t hmac[32];
    hmac256(LOCAL_KEY, frames[0].data(), 48, hmac);
    CHECK(memcmp(hmac, frames[0].data() + 48, 32) == 0);

    CHECK(getU32(frames[1].data() + 8) == 0x05);  // cmd5

    // status request: HMAC keyed with the *session* key proves key agreement
    CHECK(getU32(frames[2].data() + 8) == 0x10);
    const std::string query = decryptV34Frame(frames[2], sessionKey);
    CHECK(query.find("\"gwId\":\"mock-device-id\"") != std::string::npos);
    CHECK(query.find("\"devId\":\"mock-device-id\"") != std::string::npos);
    CHECK(query.find("\"uid\":\"mock-device-id\"") != std::string::npos);
    CHECK(query.find("\"dps\":{}") != std::string::npos);
    CHECK(query.find("\"t\":\"") != std::string::npos);

    // control frame: version header + JSON with the right dps and timestamp
    std::string ctrl = decryptV34Frame(frames[4], sessionKey);
    CHECK(ctrl.substr(0, 3) == "3.4");
    CHECK(ctrl.find("\"t\":1700000000") != std::string::npos);
    CHECK(ctrl.find("\"dps\":{\"1\":true}") != std::string::npos);

    // setDps int frame
    std::string dps = decryptV34Frame(frames[5], sessionKey);
    CHECK(dps.find("\"dps\":{\"7\":42}") != std::string::npos);
  }
}

static void testV34Heartbeat(const std::vector<std::vector<uint8_t>>& frames, const uint8_t* sessionKey) {
  // heartbeat frame: cmd 9, payload = encrypted empty (16 bytes of PKCS7 0x10)
  for (auto& f : frames) {
    if (getU32(f.data() + 8) == 0x09) {
      std::string hb = decryptV34Frame(f, sessionKey);
      CHECK(hb.empty());
      return;
    }
  }
  CHECK(false && "no heartbeat frame found");
}

static void testV34Fragmented() {
  printf("-- v3.4 fragmented (1 byte per read) --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);

  MockClient mock;
  mock.chunkLimit = 1;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(192, 168, 1, 50), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(2000);

  mock.preload(v34Cmd4Response());
  mock.preload(v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", sessionKey, false));

  bool on = true;
  CHECK(dev.getRelayState(on, "1", false));
  CHECK(on == false);
}

static void testV35() {
  printf("-- v3.5 --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV35(sessionKey);

  MockClient mock;
  LynkTuyaDevice<TUYA_V35> dev(IPAddress(192, 168, 1, 51), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(300);
  dev.onDpsUpdate(onPush);
  pushedJson.clear();

  mock.preload(v35Cmd4Response());                                                    // handshake
  mock.preload(v35JsonFrame(3, 0x10, "{\"dps\":{\"1\":true,\"9\":0}}", sessionKey, false));  // getStatus
  mock.preload(v35JsonFrame(4, 0x08, "{\"data\":{\"dps\":{\"1\":false}}}", sessionKey, true));  // push before ack
  mock.preload(v35EmptyAck(5, 0x0d, sessionKey));                                     // turnOn
  mock.preload(v35EmptyAck(6, 0x09, sessionKey));                                     // heartbeat

  String json;
  CHECK(dev.getStatus(json, false));
  CHECK(json == "{\"dps\":{\"1\":true,\"9\":0}}");

  // turnOn() without a timestamp uses the (real) system clock
  CHECK(dev.turnOn(false));
  CHECK(pushedJson.size() == 1);
  CHECK(pushedJson[0] == "{\"data\":{\"dps\":{\"1\":false}}}");

  CHECK(dev.heartbeat());

  // ---- Inspect sent frames ----
  auto frames = splitV35Frames(mock.tx);
  CHECK(frames.size() == 5);  // cmd3, cmd5, status, control, heartbeat
  if (frames.size() == 5) {
    CHECK(getU32(frames[0].data() + 10) == 0x03);
    CHECK(getU32(frames[1].data() + 10) == 0x05);

    // status request decrypts under the expected session key -> key agreement works
    CHECK(getU32(frames[2].data() + 10) == 0x10);
    const std::string query = decryptV35Frame(frames[2], sessionKey);
    CHECK(query.find("\"devId\":\"mock-device-id\"") != std::string::npos);
    CHECK(query.find("\"dps\":{}") != std::string::npos);

    // control frame: version header + JSON with a sane clock timestamp
    std::string ctrl = decryptV35Frame(frames[3], sessionKey);
    CHECK(ctrl.substr(0, 3) == "3.5");
    CHECK(ctrl.find("\"dps\":{\"1\":true}") != std::string::npos);
    size_t tPos = ctrl.find("\"t\":");
    CHECK(tPos != std::string::npos);
    if (tPos != std::string::npos)
      CHECK(atol(ctrl.c_str() + tPos + 4) > 1000000000L);  // clock was set

    // heartbeat: empty payload, valid GCM tag under the session key
    CHECK(getU32(frames[4].data() + 10) == 0x09);
    CHECK(decryptV35Frame(frames[4], sessionKey).empty());
  }
}

static void testV35Fragmented() {
  printf("-- v3.5 fragmented (7 bytes per read) --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV35(sessionKey);

  MockClient mock;
  mock.chunkLimit = 7;
  LynkTuyaDevice<TUYA_V35> dev(IPAddress(192, 168, 1, 51), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(2000);

  mock.preload(v35Cmd4Response());
  mock.preload(v35JsonFrame(3, 0x10, "{\"dps\":{\"1\":true}}", sessionKey, false));

  bool on = false;
  CHECK(dev.getRelayState(on, "1", false));
  CHECK(on == true);
}

static void testShortKey() {
  printf("-- short local key --\n");
  // Must not crash or read out of bounds; key is zero-padded.
  MockClient mock;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), "abc", mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(50);
  CHECK(dev.getStatus(false) == false);  // no scripted responses -> times out cleanly
}

// A frame whose length field is near UINT32_MAX must be rejected, not cause an
// out-of-bounds read into the 256-byte response buffer.
static void testMalformedLength() {
  printf("-- malformed frame length --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);

  MockClient mock;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(200);

  mock.preload(v34Cmd4Response());  // handshake succeeds

  // Hand-built cmd 0x10 frame with a bogus length field (0xFFFFFFFF).
  std::vector<uint8_t> bad = {0x00, 0x00, 0x55, 0xAA};
  putU32(bad, 3);
  putU32(bad, 0x10);
  putU32(bad, 0xFFFFFFFF);           // length -> would wrap `total` if unchecked
  for (int i = 0; i < 8; i++) bad.push_back(0x00);
  mock.preload(bad);

  bool on;
  CHECK(dev.getRelayState(on, "1", false) == false);  // rejected, no crash/OOB
  CHECK(dev.lastError() == LynkTuyaDevice<TUYA_V34>::Error::MalformedFrame);
}

// A v3.4 frame whose payload is not a multiple of 16 must be rejected rather
// than decrypted as a partial block.
static void resignV34(std::vector<uint8_t>& frame, const uint8_t* key);
static void testUnalignedPayload() {
  printf("-- unaligned v3.4 payload --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);

  MockClient mock;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(200);

  mock.preload(v34Cmd4Response());

  // header(16) + retcode(4) + 1 payload byte + hmac(32) + suffix(4): payload
  // length 1 is not block-aligned.
  std::vector<uint8_t> bad = {0x00, 0x00, 0x55, 0xAA};
  putU32(bad, 3);
  putU32(bad, 0x10);
  putU32(bad, 4 + 1 + 32 + 4);  // length after header
  putU32(bad, 0);              // retcode
  bad.push_back(0x42);         // 1 unaligned payload byte
  bad.insert(bad.end(), 32, 0x00);
  bad.insert(bad.end(), {0x00, 0x00, 0xAA, 0x55});
  resignV34(bad, sessionKey);

  mock.preload(bad);

  bool on;
  CHECK(dev.getRelayState(on, "1", false) == false);
  CHECK(dev.lastError() == LynkTuyaDevice<TUYA_V34>::Error::MalformedFrame);
}

static void resignV34(std::vector<uint8_t>& frame, const uint8_t* key) {
  uint8_t digest[32];
  hmac256(key, frame.data(), frame.size() - 36, digest);
  memcpy(frame.data() + frame.size() - 36, digest, 32);
}

static void testAuthenticatedFailures() {
  printf("-- v3.4 authenticated failures --\n");
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);
  auto runStatus = [&](std::vector<uint8_t> frame, LynkTuyaDevice<TUYA_V34>::Error expected) {
    MockClient mock;
    LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    dev.setTimeout(30);
    mock.preload(v34Cmd4Response());
    mock.preload(frame);
    String json;
    CHECK(!dev.getStatus(json, false));
    CHECK(dev.lastError() == expected);
  };
  auto valid = v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":true}}", sessionKey, false);
  auto badHmac = valid;
  badHmac[badHmac.size() - 37] ^= 1;
  runStatus(badHmac, LynkTuyaDevice<TUYA_V34>::Error::Authentication);
  auto badSuffix = valid;
  badSuffix.back() ^= 1;
  runStatus(badSuffix, LynkTuyaDevice<TUYA_V34>::Error::Authentication);
  auto badReturn = valid;
  badReturn[19] = 1;
  resignV34(badReturn, sessionKey);
  runStatus(badReturn, LynkTuyaDevice<TUYA_V34>::Error::DeviceReturnCode);
  uint8_t invalidPad[16] = {'{', '}', 0};
  ecb(sessionKey, invalidPad, sizeof(invalidPad), MBEDTLS_AES_ENCRYPT);
  runStatus(v34DeviceFrame(3, 0x10, std::vector<uint8_t>(invalidPad, invalidPad + 16)),
            LynkTuyaDevice<TUYA_V34>::Error::DecryptionPadding);

  MockClient badHandshake;
  LynkTuyaDevice<TUYA_V34> device(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, badHandshake);
  device.setDeviceId("mock-device-id");
  device.setTimeout(30);
  auto handshake = v34Cmd4Response();
  handshake[handshake.size() - 36] ^= 1;
  badHandshake.preload(handshake);
  CHECK(!device.getStatus(false));
  CHECK(device.lastError() == LynkTuyaDevice<TUYA_V34>::Error::Authentication);
  CHECK(splitV34Frames(badHandshake.tx).size() == 1);  // no command 5 or status

  uint8_t noncePayload[48] = {};
  memcpy(noncePayload, DEVICE_NONCE, 16);  // inner nonce HMAC intentionally invalid
  ecb(LOCAL_KEY, noncePayload, 48, MBEDTLS_AES_ENCRYPT);
  MockClient badNonce;
  LynkTuyaDevice<TUYA_V34> nonceDevice(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, badNonce);
  nonceDevice.setDeviceId("mock-device-id");
  nonceDevice.setTimeout(30);
  badNonce.preload(v34DeviceFrame(1, 0x04, std::vector<uint8_t>(noncePayload, noncePayload + 48)));
  CHECK(!nonceDevice.getStatus(false));
}

static void testProxyContentionModel() {
  printf("-- proxy contention model --\n");
  MockClient occupied;
  occupied.allowConnect = false;  // Another LAN client occupies the device socket.
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, occupied);
  dev.setDeviceId("mock-device-id");
  CHECK(!dev.getStatus(false));
  CHECK(dev.lastError() == LynkTuyaDevice<TUYA_V34>::Error::Connect);
  CHECK(occupied.tx.empty());
  CHECK(!dev.turnOn((uint32_t)1800000000, false));
  CHECK(occupied.tx.empty());  // No command is sent or retried on contention.
}

static void testPersistentPushAndHealth() {
  printf("-- persistent v3.4 pushes, heartbeat, and reconnect --\n");
  using Device = LynkTuyaDevice<TUYA_V34>;
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);
  MockClient mock;
  Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setTimeout(30);
  ObservedRelay observed;
  dev.onDpsUpdate(onVerifiedRelay, &observed);
  pushedJson.clear();
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", sessionKey, false));
  CHECK(dev.getStatus(false));
  CHECK(dev.sessionReady());
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);
  mock.preload(v34JsonFrame(4, 0x08, "{\"data\":{\"dps\":{\"1\":true}}}", sessionKey, true));
  CHECK(dev.pollFrame() == Device::PollResult::Frame);
  CHECK(pushedJson.size() == 1);
  CHECK(pushedJson[0].find("\"1\":true") != std::string::npos);
  CHECK(observed.known && observed.on && observed.updates == 1);
  // The authenticated event updates the state consumed by TFT and Web without
  // waiting for a status query or the fallback reconciliation interval.
  const auto afterFirstPush = splitV34Frames(mock.tx);
  CHECK(std::count_if(afterFirstPush.begin(), afterFirstPush.end(),
                      [](const std::vector<uint8_t>& frame) {
                        return getU32(frame.data() + 8) == 0x10;
                      }) == 1);
  mock.preload(v34JsonFrame(5, 0x08, "{\"data\":{\"dps\":{\"1\":false}}}", sessionKey, true));
  mock.preload(v34EmptyAck(6, 0x09));
  CHECK(dev.heartbeat(false));
  CHECK(pushedJson.size() == 2);
  CHECK(pushedJson[1].find("\"1\":false") != std::string::npos);
  CHECK(observed.known && !observed.on && observed.updates == 2);
  mock.preload(v34EmptyAck(7, 0x0d));
  mock.preload(v34JsonFrame(8, 0x10, "{\"dps\":{\"1\":false}}", sessionKey, false));
  CHECK(dev.turnOff((uint32_t)1800000000, false));
  bool verifiedOff = true;
  CHECK(dev.getRelayState(verifiedOff, "1", false));
  CHECK(!verifiedOff);
  auto beforeReconnect = splitV34Frames(mock.tx);
  CHECK(std::count_if(beforeReconnect.begin(), beforeReconnect.end(),
                      [](const std::vector<uint8_t>& frame) { return getU32(frame.data() + 8) == 0x03; }) == 1);
  auto malformed = v34JsonFrame(7, 0x08, "{\"data\":{\"dps\":{\"1\":true}}}", sessionKey, true);
  malformed[malformed.size() - 36] ^= 1;
  mock.preload(malformed);
  CHECK(dev.pollFrame() == Device::PollResult::Invalid);
  CHECK(dev.lastError() == Device::Error::Authentication);
  CHECK(pushedJson.size() == 2);
  CHECK(observed.updates == 2);
  mock.preload(v34JsonFrame(9, 0x08, "{\"data\":{\"dps\":{\"2\":true}}}", sessionKey, true));
  CHECK(dev.pollFrame() == Device::PollResult::Frame);
  CHECK(observed.known && !observed.on && observed.updates == 2);
  mock.preload(v34JsonFrame(10, 0x08, "{\"data\":{\"dps\":{\"1\":1}}}", sessionKey, true));
  CHECK(dev.pollFrame() == Device::PollResult::Frame);
  CHECK(observed.known && !observed.on && observed.updates == 2);
  dev.closeConnection();
  CHECK(dev.pollFrame() == Device::PollResult::Disconnected);
  uint8_t nextNonce[16];
  for (size_t i = 0; i < 16; ++i) nextNonce[i] = LOCAL_NONCE[i] + 1;
  uint8_t nextSessionKey[16];
  expectedSessionKeyV34(nextSessionKey, nextNonce);
  esp_random_test_offset() = 1;
  mock.preload(v34Cmd4Padded(true, false, false, false, false, nextNonce));
  auto recoveredStatus = v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", nextSessionKey, false);
  uint8_t recoveredDigest[32];
  hmac256(nextSessionKey, recoveredStatus.data(), recoveredStatus.size() - 36, recoveredDigest);
  memcpy(recoveredStatus.data() + recoveredStatus.size() - 36, recoveredDigest, 32);
  mock.preload(recoveredStatus);
  CHECK(dev.getStatus(false));
  esp_random_test_offset() = 0;
  CHECK(dev.sessionReady());
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);
  const auto afterReconnect = splitV34Frames(mock.tx);
  std::vector<std::vector<uint8_t>> challenges;
  for (const auto& frame : afterReconnect)
    if (getU32(frame.data() + 8) == 0x03) challenges.push_back(frame);
  CHECK(challenges.size() == 2);
  if (challenges.size() == 2) CHECK(challenges[0] != challenges[1]);
  CHECK(!dev.heartbeat(false));  // Missing heartbeat ACK is a connection-health failure.
  CHECK(dev.lastError() == Device::Error::Timeout);
  dev.closeConnection();
  uint8_t thirdNonce[16];
  for (size_t i = 0; i < 16; ++i) thirdNonce[i] = LOCAL_NONCE[i] + 2;
  uint8_t thirdSessionKey[16];
  expectedSessionKeyV34(thirdSessionKey, thirdNonce);
  esp_random_test_offset() = 2;
  mock.preload(v34Cmd4Padded(true, false, false, false, false, thirdNonce));
  auto thirdStatus = v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":true}}", thirdSessionKey, false);
  uint8_t thirdDigest[32];
  hmac256(thirdSessionKey, thirdStatus.data(), thirdStatus.size() - 36, thirdDigest);
  memcpy(thirdStatus.data() + thirdStatus.size() - 36, thirdDigest, 32);
  mock.preload(thirdStatus);
  bool relayOn = false;
  CHECK(dev.getRelayState(relayOn, "1", false));
  CHECK(relayOn);
  esp_random_test_offset() = 0;
  const auto afterHealthRecovery = splitV34Frames(mock.tx);
  CHECK(std::count_if(afterHealthRecovery.begin(), afterHealthRecovery.end(),
                      [](const std::vector<uint8_t>& frame) {
                        return getU32(frame.data() + 8) == 0x03;
                      }) == 3);
}

static void testIncrementalPushFrames() {
  printf("-- incremental authenticated push framing and deadline --\n");
  using Device = LynkTuyaDevice<TUYA_V34>;
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);
  MockClient mock;
  Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
  dev.setDeviceId("mock-device-id");
  dev.setPartialFrameTimeout(300);
  ObservedRelay observed;
  dev.onDpsUpdate(onVerifiedRelay, &observed);
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  CHECK(dev.getStatus(false));
  const auto frame = v34JsonFrame(4, 0x08,
      "{\"data\":{\"dps\":{\"1\":true}}}", sessionKey, true);
  useFakeMillis() = true;
  fakeMillis() = UINT32_MAX - 100;
  mock.preload(std::vector<uint8_t>(frame.begin(), frame.begin() + 5));
  const auto started = std::chrono::steady_clock::now();
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started).count();
  CHECK(elapsed < 100);  // Mock wall time; not a hardware scheduling bound.
  CHECK(observed.updates == 0);
  fakeMillis() += 150;  // Cross uint32_t wraparound before the rest arrives.
  mock.preload(std::vector<uint8_t>(frame.begin() + 5, frame.end()));
  CHECK(dev.pollFrame() == Device::PollResult::Frame);
  CHECK(observed.updates == 1 && observed.on);
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);

  // One byte per poll exercises header, body, HMAC, and suffix boundaries.
  mock.chunkLimit = 1;
  mock.preload(frame);
  size_t partialCalls = 0;
  Device::PollResult splitResult = Device::PollResult::NoFrame;
  for (; partialCalls < frame.size(); ++partialCalls) {
    splitResult = dev.pollFrame();
    if (splitResult != Device::PollResult::NoFrame) break;
  }
  CHECK(partialCalls > 0 && splitResult == Device::PollResult::Frame);
  CHECK(observed.updates == 2 && observed.on);
  mock.chunkLimit = SIZE_MAX;

  auto tampered = frame;
  tampered[tampered.size() - 36] ^= 1;
  mock.preload(std::vector<uint8_t>(tampered.begin(), tampered.begin() + 16));
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);
  mock.preload(std::vector<uint8_t>(tampered.begin() + 16, tampered.end()));
  CHECK(dev.pollFrame() == Device::PollResult::Invalid);
  CHECK(dev.lastError() == Device::Error::Authentication);
  CHECK(observed.updates == 2);

  mock.preload(std::vector<uint8_t>(frame.begin(), frame.begin() + 8));
  CHECK(dev.pollFrame() == Device::PollResult::NoFrame);
  fakeMillis() += 301;
  CHECK(dev.pollFrame() == Device::PollResult::Invalid);
  CHECK(dev.lastError() == Device::Error::IncompleteFrame);
  CHECK(observed.updates == 2);
  // A timed-out partial frame invalidates the session in the wrapper; the
  // transport itself must be closed by its owner before parsing new frames.
  dev.closeConnection();
  useFakeMillis() = false;
}

static TuyaLocalClient::Config clientConfig();
static void testPollBlockingBoundary() {
  printf("-- poll latency boundary under missing handshake response --\n");
  MockClient mock;
  TuyaLocalClient client;
  auto config = clientConfig();
  config.commandResponseTimeoutMs = 100;
  CHECK(client.begin(config, {}, &mock));
  const auto start = std::chrono::steady_clock::now();
  CHECK(client.poll() == TuyaLocalClient::PollResult::Disconnected);
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start).count();
  printf("  missing-handshake poll: %lld ms (host mock, 100 ms response timeout)\n",
         static_cast<long long>(elapsed));
  CHECK(elapsed >= 90 && elapsed < 1000);
  client.end();
}

static void testControlAcknowledgements() {
  printf("-- v3.4 authenticated ON/OFF acknowledgements --\n");
  MockClient mock;
  LynkTuyaDevice<TUYA_V34> dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
  dev.setTimeout(30);
  mock.preload(v34Cmd4Response());
  auto badAck = v34EmptyAck(3, 0x0d);
  badAck[badAck.size() - 36] ^= 1;
  mock.preload(badAck);
  CHECK(!dev.turnOn((uint32_t)1800000000, false));
  auto frames = splitV34Frames(mock.tx);
  CHECK(frames.size() == 3);  // ON sent once despite uncertain outcome.
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);
  CHECK(decryptV34Frame(frames[2], sessionKey).find("\"1\":true") != std::string::npos);

  MockClient offMock;
  LynkTuyaDevice<TUYA_V34> offDev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, offMock);
  offDev.setTimeout(30);
  offMock.preload(v34Cmd4Response());
  offMock.preload(v34EmptyAck(3, 0x0d));
  CHECK(offDev.turnOff((uint32_t)1800000000, false));
  auto offFrames = splitV34Frames(offMock.tx);
  CHECK(offFrames.size() == 3);
  CHECK(decryptV34Frame(offFrames[2], sessionKey).find("\"1\":false") != std::string::npos);
}

static void testV34ResponseVariantsAndDiagnostics() {
  printf("-- v3.4 response variants and safe diagnostics --\n");
  using Device = LynkTuyaDevice<TUYA_V34>;
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == Device::Error::MissingDeviceId);
    CHECK(mock.tx.empty());
  }
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    auto wrong = v34DeviceFrameNoReturn(1, 0x11, {});
    resignV34(wrong, LOCAL_KEY);
    mock.preload(wrong);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == Device::Error::HandshakeFailure);
    CHECK(dev.lastResponseType() == Device::ResponseType::Other);
  }
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    mock.preload(v34Cmd4NoReturn());
    mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
    String json;
    CHECK(dev.getStatus(json, false));
    CHECK(json == "{\"dps\":{\"1\":false}}");
    CHECK(dev.lastResponseCommand() == 0x10);
    CHECK(dev.lastResponseType() == Device::ResponseType::Status);
    CHECK(dev.lastFrameLength() == v34StatusNoReturn("{\"dps\":{\"1\":false}}").size());
    CHECK(dev.lastError() == Device::Error::None);
  }
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    mock.preload(v34Cmd4Response());
    auto wrong = v34DeviceFrameNoReturn(3, 0x11, {});
    mock.preload(wrong);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == Device::Error::UnsupportedResponseCommand);
    CHECK(dev.lastResponseCommand() == 0x11);
    CHECK(dev.lastResponseType() == Device::ResponseType::Other);
    CHECK(dev.lastFrameLength() == wrong.size());
  }
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    mock.preload(v34Cmd4Response());
    auto bad = v34StatusNoReturn("{\"dps\":{\"1\":true}}");
    bad[bad.size() - 36] ^= 1;
    mock.preload(bad);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == Device::Error::Authentication);
    CHECK(dev.lastResponseCommand() == 0x10);
  }
  {
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    mock.preload(v34Cmd4Response());
    auto truncated = v34StatusNoReturn("{\"dps\":{\"1\":true}}");
    truncated.resize(truncated.size() - 3);
    mock.preload(truncated);
    dev.setTimeout(20);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == Device::Error::IncompleteFrame);
  }
}

static void testV34PaddedHandshake() {
  printf("-- v3.4 padded handshake and malformed variants --\n");
  using Device = LynkTuyaDevice<TUYA_V34>;
  uint8_t sessionKey[16];
  expectedSessionKeyV34(sessionKey);
  for (int variant = 0; variant < 3; ++variant) {
    const bool retcode = variant != 1;
    const bool version = variant == 2;
    auto handshake = v34Cmd4Padded(retcode, version);
    CHECK(handshake.size() == (retcode ? 120u : 116u));
    CHECK(getU32(handshake.data() + 12) == handshake.size() - 16);
    uint8_t digest[32];
    hmac256(LOCAL_KEY, handshake.data(), handshake.size() - 36, digest);
    CHECK(memcmp(digest, handshake.data() + handshake.size() - 36, 32) == 0);
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    mock.preload(handshake);
    mock.preload(v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", sessionKey, false));
    bool on = true;
    CHECK(dev.getRelayState(on, "1", false));
    CHECK(!on);
    CHECK(dev.lastError() == Device::Error::None);
    CHECK(splitV34Frames(mock.tx).size() == 3);
  }

  for (int variant = 0; variant < 7; ++variant) {
    auto handshake = v34Cmd4Padded(true,
                                    variant == 1 || variant == 2,
                                    variant == 0 || variant == 1,
                                    variant == 3,
                                    variant == 2);
    Device::Error expected = Device::Error::MalformedFrame;
    if (variant == 0 || variant == 1) expected = Device::Error::DecryptionPadding;
    if (variant == 3 || variant == 4) expected = Device::Error::Authentication;
    if (variant == 5) expected = Device::Error::DeviceReturnCode;
    if (variant == 6) expected = Device::Error::IncompleteFrame;
    if (variant == 4) handshake[handshake.size() - 36] ^= 1;
    if (variant == 5) { handshake[19] = 1; resignV34(handshake, LOCAL_KEY); }
    if (variant == 6) handshake.resize(handshake.size() - 1);
    MockClient mock;
    Device dev(IPAddress(1, 2, 3, 4), (const char*)LOCAL_KEY, mock);
    dev.setDeviceId("mock-device-id");
    dev.setTimeout(20);
    mock.preload(handshake);
    CHECK(!dev.getStatus(false));
    CHECK(dev.lastError() == expected);
    CHECK(splitV34Frames(mock.tx).size() == 1);
  }
}

struct ClientEvents {
  int status = 0;
  int push = 0;
  int connects = 0;
  int disconnects = 0;
  int commands = 0;
  bool lastRequested = false;
  bool lastAck = false;
  String lastJson;
  String lastDps;
  int mapped = 0;
  uint16_t mappedId = 0;
  bool mappedValue = false;
  bool mappedPush = false;
  int logs = 0;
  TuyaLocalClient::Error lastLogError = TuyaLocalClient::Error::None;
  TuyaLocalClient* reentrantClient = nullptr;
  bool reentrantAccepted = false;
};

static void clientStatus(void* context, const String& json) {
  auto& events = *static_cast<ClientEvents*>(context);
  ++events.status;
  events.lastJson = json;
  if (events.reentrantClient)
    events.reentrantAccepted = events.reentrantClient->requestBooleanDps(
        "1", true, 1800000000);
}
static void clientPush(void* context, const String& json) {
  auto& events = *static_cast<ClientEvents*>(context);
  ++events.push;
  events.lastJson = json;
}
static void clientConnection(void* context, bool connected) {
  auto& events = *static_cast<ClientEvents*>(context);
  if (connected) ++events.connects; else ++events.disconnects;
}
static void clientCommand(void* context, const char* dps, bool requested, bool ack) {
  auto& events = *static_cast<ClientEvents*>(context);
  events.lastDps = dps ? dps : "";
  ++events.commands;
  events.lastRequested = requested;
  events.lastAck = ack;
}
static void clientMapped(void* context, uint16_t id, bool value, bool push) {
  auto& events = *static_cast<ClientEvents*>(context);
  ++events.mapped;
  events.mappedId = id;
  events.mappedValue = value;
  events.mappedPush = push;
}
static void clientDiagnostic(void* context, const TuyaLocalClient::DiagnosticEvent& event) {
  auto& events = *static_cast<ClientEvents*>(context);
  ++events.logs;
  events.lastLogError = event.error;
}

static TuyaLocalClient::Config clientConfig() {
  TuyaLocalClient::Config config;
  config.host = IPAddress(1, 2, 3, 4);
  config.deviceId = "mock-device-id";
  config.localKey = (const char*)LOCAL_KEY;
  config.commandResponseTimeoutMs = 100;
  config.heartbeatIntervalMs = 1000;
  config.statusReconciliationMs = 1000;
  return config;
}

static void testTuyaLocalClient() {
  printf("-- TuyaLocalClient lifecycle and authenticated API --\n");
  uint8_t key[16];
  expectedSessionKeyV34(key);
  MockClient mock;
  ClientEvents events;
  TuyaLocalClient client;
  auto config = clientConfig();
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onStatus = clientStatus;
  callbacks.onPush = clientPush;
  callbacks.onConnection = clientConnection;
  callbacks.onCommand = clientCommand;
  callbacks.context = &events;
  CHECK(client.begin(config, callbacks, &mock));
  CHECK(!client.connected());
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", key, false));
  CHECK(client.poll() == TuyaLocalClient::PollResult::Frame);
  CHECK(client.connected());
  CHECK(events.status == 1 && events.connects == 1);
  CHECK(events.lastJson.indexOf("\"1\":false") >= 0);
  CHECK(client.poll() == TuyaLocalClient::PollResult::NoFrame);

  mock.preload(v34JsonFrame(4, 0x08, "{\"data\":{\"dps\":{\"1\":true}}}", key, true));
  CHECK(client.poll() == TuyaLocalClient::PollResult::Frame);
  CHECK(events.push == 1 && events.lastJson.indexOf("\"1\":true") >= 0);
  CHECK(client.diagnostics().pushEvents == 1);
  mock.preload(v34EmptyAck(5, 0x0d));
  CHECK(client.setBooleanDps("1", false, 1800000000));
  CHECK(events.commands == 1 && events.lastDps == "1" &&
        !events.lastRequested && events.lastAck);
  mock.preload(v34JsonFrame(6, 0x10, "{\"dps\":{\"1\":false}}", key, false));
  String json;
  CHECK(client.readStatus(json));
  CHECK(json.indexOf("\"1\":false") >= 0);
  CHECK(events.status == 1);  // Explicit read returns data to its caller.
  mock.preload(v34EmptyAck(7, 0x0d));
  CHECK(client.setBooleanDps("1", true, 1800000000));
  CHECK(events.commands == 2 && events.lastRequested && events.lastAck);
  mock.preload(v34JsonFrame(8, 0x10, "{\"dps\":{\"1\":true}}", key, false));
  CHECK(client.readStatus(json));
  CHECK(json.indexOf("\"1\":true") >= 0);
  CHECK(client.diagnostics().sessions == 1);  // Sequential app calls share one socket.

  auto bad = v34JsonFrame(9, 0x08, "{\"data\":{\"dps\":{\"1\":true}}}", key, true);
  bad[bad.size() - 36] ^= 1;
  mock.preload(bad);
  CHECK(client.poll() == TuyaLocalClient::PollResult::Invalid);
  CHECK(!client.connected());
  CHECK(client.diagnostics().authenticationErrors == 1);
  CHECK(events.push == 1);  // Unauthenticated push was discarded.

  client.end();
  CHECK(!client.connected());
  CHECK(client.diagnostics().sessions == 0);
  CHECK(client.begin(config, callbacks, &mock));
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  CHECK(client.readStatus(json));
  CHECK(client.diagnostics().sessions == 1);
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":true}}"));
  CHECK(client.reconnect(json));
  CHECK(client.diagnostics().sessions == 2);
  CHECK(json.indexOf("\"1\":true") >= 0);
  CHECK(client.poll(false) == TuyaLocalClient::PollResult::Disconnected);
  CHECK(!client.connected());
  CHECK(events.disconnects >= 2);
  client.end();

  config.localKey = "short";
  CHECK(!client.begin(config, callbacks, &mock));
  CHECK(!mock.isConn);
  config = clientConfig();
  config.deviceId = "invalid\"id";
  CHECK(!client.begin(config, callbacks, &mock));
}

static void testTuyaLocalClientFailures() {
  printf("-- TuyaLocalClient timeout, heartbeat, and reconnect --\n");
  uint8_t key[16];
  expectedSessionKeyV34(key);
  {
    MockClient unauthenticated;
    TuyaLocalClient candidate;
    CHECK(candidate.begin(clientConfig(), {}, &unauthenticated));
    auto reply = v34Cmd4Padded(true);
    reply[reply.size() - 36] ^= 1;
    unauthenticated.preload(reply);
    CHECK(!candidate.connect());
    CHECK(candidate.diagnostics().authenticationErrors == 1);
    CHECK(!candidate.connected());
  }
  {
    MockClient malformed;
    TuyaLocalClient candidate;
    CHECK(candidate.begin(clientConfig(), {}, &malformed));
    auto reply = v34Cmd4Padded(true);
    reply[0] ^= 1;
    malformed.preload(reply);
    CHECK(!candidate.connect());
    CHECK(candidate.diagnostics().invalidResponses == 1);
  }
  MockClient mock;
  TuyaLocalClient client;
  auto config = clientConfig();
  CHECK(client.begin(config, {}, &mock));
  CHECK(!client.setBooleanDps("1\"}...", true, 1800000000));
  CHECK(mock.tx.empty());
  mock.allowConnect = false;
  CHECK(!client.connect());
  CHECK(client.diagnostics().connectionErrors == 1);
  client.end();

  mock.allowConnect = true;
  CHECK(client.begin(config, {}, &mock));
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  String json;
  CHECK(client.readStatus(json));
  delay(1002);
  // No heartbeat ACK: the owner closes the session and schedules backoff.
  CHECK(client.poll() == TuyaLocalClient::PollResult::Disconnected);
  CHECK(client.diagnostics().heartbeatFailures == 1);
  CHECK(!client.connected());
  CHECK(client.poll() == TuyaLocalClient::PollResult::NoFrame);
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  CHECK(client.reconnect(json));
  CHECK(client.connected() && client.diagnostics().sessions == 2);
  client.end();

  CHECK(client.begin(config, {}, &mock));
  mock.preload(v34Cmd4Padded(true));
  CHECK(!client.readStatus(json));
  CHECK(client.diagnostics().connectionErrors >= 1);
  CHECK(!client.connected());
  client.end();

  CHECK(client.begin(config, {}, &mock));
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  CHECK(client.readStatus(json));
  // An unacknowledged write is uncertain; the wrapper never resends it.
  const uint32_t beforeTimeout = millis();
  CHECK(!client.setBooleanDps("1", true, 1800000000));
  const uint32_t elapsed = millis() - beforeTimeout;
  CHECK(elapsed >= 90 && elapsed < 1000);  // Configured 100 ms response wait.
  const auto sent = splitV34Frames(mock.tx);
  CHECK(std::count_if(sent.begin(), sent.end(), [](const std::vector<uint8_t>& frame) {
    return getU32(frame.data() + 8) == 0x0d;
  }) == 1);
  client.end();
}

static void testTuyaLocalConfiguration() {
  printf("-- TuyaLocalConfig validation, mapping, timing, and retries --\n");
  MockClient mock;
  TuyaLocalClient client;
  auto config = clientConfig();
  config.port = 16668;
  config.dps.stateBoolean = 2;
  config.dps.controlBoolean = 3;
  config.heartbeatIntervalMs = 2000;
  config.reconnectMinimumMs = 250;
  config.reconnectMaximumMs = 500;
  config.logging = TuyaLocalConfig::LogLevel::ProtocolMetadata;
  ClientEvents events;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onStatus = clientStatus;
  callbacks.onMappedBoolean = clientMapped;
  callbacks.onCommand = clientCommand;
  callbacks.onDiagnostic = clientDiagnostic;
  callbacks.context = &events;
  CHECK(client.begin(config, callbacks, &mock));
  uint8_t key[16];
  expectedSessionKeyV34(key);
  mock.preload(v34Cmd4Padded(true));
  mock.preload(v34StatusNoReturn("{\"dps\":{\"2\":false}}"));
  CHECK(client.poll() == TuyaLocalClient::PollResult::Frame);
  CHECK(mock.lastPort == 16668);
  CHECK(events.mapped == 1 && events.mappedId == 2 && !events.mappedValue);
  CHECK(events.logs >= 2 && events.lastLogError == TuyaLocalClient::Error::None);
  mock.preload(v34JsonFrame(4, 0x08,
      "{\"data\":{\"dps\":{\"2\":true}}}", key, true));
  CHECK(client.poll() == TuyaLocalClient::PollResult::Frame);
  CHECK(events.mapped == 2 && events.mappedValue && events.mappedPush);
  mock.preload(v34EmptyAck(5, 0x0d));
  CHECK(client.setMappedBoolean(true, 1800000000));
  CHECK(events.lastDps == "3" && events.lastAck);
  auto frames = splitV34Frames(mock.tx);
  CHECK(decryptV34Frame(frames.back(), key).find("\"3\":true") != std::string::npos);
  mock.preload(v34StatusNoReturn("{\"dps\":{\"2\":false}}"));
  bool mapped = true;
  CHECK(client.readMappedBoolean(mapped) && !mapped);
  CHECK(events.mapped == 3 && !events.mappedPush);
  delay(1002);
  mock.onWriteCommand = [&](uint32_t command) {
    if (command == 0x10) mock.preload(v34StatusNoReturn("{\"dps\":{\"2\":true}}"));
  };
  CHECK(client.poll() == TuyaLocalClient::PollResult::NoFrame);
  CHECK(events.status == 2 && events.mapped == 4 && events.mappedValue);
  client.end();

  // Invalid values fail before any socket operation.
  auto invalid = clientConfig();
  invalid.port = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.host = IPAddress(0, 0, 0, 0);
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.protocol = TuyaLocalConfig::ProtocolVersion::V35;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.dps.controlBoolean = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.heartbeatIntervalMs = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.statusReconciliationMs = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.connectionTimeoutMs = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.commandResponseTimeoutMs = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.partialFrameTimeoutMs = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.maxBytesPerPoll = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.maxFramesPerPoll = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.maxTransactionsPerPoll = 0;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.reconnectMaximumMs = 500;
  invalid.reconnectMinimumMs = 1000;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.retry.statusReadAttempts = 4;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.retry.controlWriteAttempts = 2;
  CHECK(!client.begin(invalid, {}, &mock));
  invalid = clientConfig();
  invalid.logging = static_cast<TuyaLocalConfig::LogLevel>(99);
  CHECK(!client.begin(invalid, {}, &mock));

  MockClient quiet;
  config = clientConfig();
  config.logging = TuyaLocalConfig::LogLevel::Errors;
  config.diagnostics.countEvents = false;
  config.diagnostics.captureResponseMetadata = false;
  CHECK(client.begin(config, callbacks, &quiet));
  auto badHandshake = v34Cmd4Padded(true);
  badHandshake[badHandshake.size() - 36] ^= 1;
  quiet.preload(badHandshake);
  const int logsBefore = events.logs;
  CHECK(!client.connect());
  CHECK(events.logs == logsBefore + 1 &&
        events.lastLogError == TuyaLocalClient::Error::Authentication);
  CHECK(client.diagnostics().authenticationErrors == 0);
  CHECK(client.diagnostics().lastCommand == 0);
  CHECK(client.diagnostics().lastError == TuyaLocalClient::Error::Authentication);
  client.end();

  MockClient backoff;
  config = clientConfig();
  config.reconnectMinimumMs = 250;
  config.reconnectMaximumMs = 500;
  CHECK(client.begin(config, {}, &backoff));
  backoff.allowConnect = false;
  CHECK(client.poll() == TuyaLocalClient::PollResult::Disconnected);
  CHECK(client.diagnostics().scheduledReconnectDelayMs == 250);
  const int firstCalls = backoff.connectCalls;
  CHECK(client.poll() == TuyaLocalClient::PollResult::NoFrame);
  CHECK(backoff.connectCalls == firstCalls);
  delay(252);
  CHECK(client.poll() == TuyaLocalClient::PollResult::Disconnected);
  CHECK(client.diagnostics().scheduledReconnectDelayMs == 500);
  client.end();

  MockClient retry;
  config = clientConfig();
  config.retry.statusReadAttempts = 2;
  CHECK(client.begin(config, {}, &retry));
  retry.preload(v34Cmd4Padded(true));
  auto invalidStatus = v34StatusNoReturn("{\"dps\":{\"1\":false}}");
  invalidStatus[invalidStatus.size() - 36] ^= 1;
  retry.preload(invalidStatus);
  retry.preload(v34Cmd4Padded(true));
  retry.preload(v34StatusNoReturn("{\"dps\":{\"1\":false}}"));
  String json;
  CHECK(client.readStatus(json));
  CHECK(client.diagnostics().authenticationErrors == 1);
  CHECK(client.diagnostics().sessions == 2);
  client.end();
}

static bool socketReadExact(int fd, uint8_t* out, size_t length) {
  size_t got = 0;
  while (got < length) {
    const int n = ::recv(fd, out + got, length - got, 0);
    if (n <= 0) return false;
    got += static_cast<size_t>(n);
  }
  return true;
}

static std::vector<uint8_t> socketReadFrame(int fd) {
  std::vector<uint8_t> frame(16);
  if (!socketReadExact(fd, frame.data(), 16)) return {};
  const uint32_t length = getU32(frame.data() + 12);
  if (length < 36 || length > 240) return {};
  frame.resize(16 + length);
  if (!socketReadExact(fd, frame.data() + 16, length)) return {};
  return frame;
}

static bool socketSendAll(int fd, const uint8_t* data, size_t length) {
  while (length) {
    const int n = ::send(fd, data, length, 0);
    if (n <= 0) return false;
    data += n;
    length -= static_cast<size_t>(n);
  }
  return true;
}

static void testNativeCooperativeSocket() {
  printf("-- native nonblocking loopback socket, fragmented handshake and push --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  socklen_t addressLength = sizeof(address);
  const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) == 0 &&
                         ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                       &addressLength) == 0 &&
                         ::listen(listener, 1) == 0;
  CHECK(listening);
  if (!listening) { ::close(listener); return; }
  std::atomic<bool> serverOk{true};
  std::thread server([&]() {
    int peer = ::accept(listener, nullptr, nullptr);
    if (peer < 0) { serverOk = false; return; }
    timeval timeout{};
    timeout.tv_sec = 3;
    ::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const auto cmd3 = socketReadFrame(peer);
    if (cmd3.size() != 84 || getU32(cmd3.data() + 8) != 0x03) serverOk = false;
    const auto cmd4 = v34Cmd4Padded(true);
    if (!socketSendAll(peer, cmd4.data(), 7)) serverOk = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    if (!socketSendAll(peer, cmd4.data() + 7, cmd4.size() - 7)) serverOk = false;
    const auto cmd5 = socketReadFrame(peer);
    if (cmd5.size() != 100 || getU32(cmd5.data() + 8) != 0x05) serverOk = false;
    const auto query = socketReadFrame(peer);
    if (query.empty() || getU32(query.data() + 8) != 0x10) serverOk = false;
    const auto status = v34StatusNoReturn("{\"dps\":{\"1\":false}}");
    if (!socketSendAll(peer, status.data(), 9)) serverOk = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    if (!socketSendAll(peer, status.data() + 9, status.size() - 9)) serverOk = false;
    const auto control = socketReadFrame(peer);
    if (control.empty() || getU32(control.data() + 8) != 0x0d) serverOk = false;
    uint8_t sessionKey[16];
    expectedSessionKeyV34(sessionKey);
    if (!control.empty() && decryptV34Frame(control, sessionKey).find("\"1\":true") ==
                                std::string::npos) serverOk = false;
    const auto push = v34JsonFrame(5, 0x08,
        "{\"data\":{\"dps\":{\"1\":true}}}", sessionKey, true);
    const auto ack = v34EmptyAck(6, 0x0d);
    if (!socketSendAll(peer, push.data(), push.size()) ||
        !socketSendAll(peer, ack.data(), ack.size())) serverOk = false;
    const auto verify = socketReadFrame(peer);
    if (verify.empty() || getU32(verify.data() + 8) != 0x10) serverOk = false;
    const auto verifiedOn = v34JsonFrame(7, 0x10, "{\"dps\":{\"1\":true}}",
                                         sessionKey, false);
    if (!socketSendAll(peer, verifiedOn.data(), verifiedOn.size())) serverOk = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ::close(peer);
  });
  TuyaLocalClient client;
  ClientEvents events;
  events.reentrantClient = &client;
  auto config = clientConfig();
  config.host = IPAddress(127, 0, 0, 1);
  config.port = ntohs(address.sin_port);
  config.connectionTimeoutMs = 1000;
  config.commandResponseTimeoutMs = 1000;
  config.maxBytesPerPoll = 16;  // Forces partial sends and receives.
  config.maxFramesPerPoll = 1;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.onStatus = clientStatus;
  callbacks.onPush = clientPush;
  callbacks.onConnection = clientConnection;
  callbacks.onCommand = clientCommand;
  callbacks.onMappedBoolean = clientMapped;
  callbacks.context = &events;
  CHECK(client.begin(config, callbacks));
  const auto started = std::chrono::steady_clock::now();
  while (events.status == 0 && std::chrono::steady_clock::now() - started <
         std::chrono::seconds(3)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(events.status == 1 && events.mapped == 1 && !events.mappedValue);
  CHECK(!events.reentrantAccepted);
  CHECK(events.connects == 1 && client.connected());
  CHECK(client.requestBooleanDps("1", true, 1800000000));
  while (events.commands == 0 && std::chrono::steady_clock::now() - started <
         std::chrono::seconds(4)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(events.push == 1 && events.mappedPush && events.mappedValue);
  CHECK(events.commands == 1 && events.lastAck);
  bool ack = false;
  CHECK(client.takeCommandResult(ack) && ack);
  String verified;
  CHECK(client.readStatus(verified));
  CHECK(verified.indexOf("\"1\":true") >= 0);
  printf("  maximum native poll: %lu us (host loopback, 16-byte budget)\n",
         static_cast<unsigned long>(client.diagnostics().maxPollUs));
  CHECK(client.diagnostics().maxPollUs < 50000);  // Host scheduling, not RT bound.
  client.end();
  server.join();
  ::close(listener);
  CHECK(serverOk.load());
}

static void testNativeMissingHandshake() {
  printf("-- native missing handshake, backoff, and socket cleanup --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) == 0 &&
                         ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                       &addressLength) == 0 &&
                         ::listen(listener, 1) == 0;
  CHECK(listening);
  if (!listening) { ::close(listener); return; }
  std::atomic<bool> peerClosed{false};
  std::thread server([&]() {
    int peer = ::accept(listener, nullptr, nullptr);
    if (peer < 0) return;
    timeval timeout{};
    timeout.tv_sec = 2;
    ::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    const auto cmd3 = socketReadFrame(peer);
    if (!cmd3.empty() && getU32(cmd3.data() + 8) == 0x03) {
      uint8_t byte;
      peerClosed = ::recv(peer, &byte, 1, 0) == 0;
    }
    ::close(peer);
  });
  TuyaLocalClient client;
  auto config = clientConfig();
  config.host = IPAddress(127, 0, 0, 1);
  config.port = ntohs(address.sin_port);
  config.connectionTimeoutMs = 500;
  config.commandResponseTimeoutMs = 100;
  config.maxBytesPerPoll = 16;
  CHECK(client.begin(config));
  const auto started = std::chrono::steady_clock::now();
  while (client.diagnostics().connectionErrors == 0 &&
         std::chrono::steady_clock::now() - started < std::chrono::seconds(2)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(client.diagnostics().lastError == TuyaLocalClient::Error::Timeout);
  CHECK(client.diagnostics().scheduledReconnectDelayMs == config.reconnectMinimumMs);
  CHECK(!client.connected());
  CHECK(client.diagnostics().maxPollUs < 50000);
  server.join();
  CHECK(peerClosed.load());
  client.end();
  ::close(listener);
}

static void testNativeRecoveryAndHeartbeat() {
  printf("-- native status timeout, reconnect, and heartbeat failure --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) == 0 &&
                         ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                       &addressLength) == 0 &&
                         ::listen(listener, 2) == 0;
  CHECK(listening);
  if (!listening) { ::close(listener); return; }
  std::atomic<bool> serverOk{true};
  std::thread server([&]() {
    for (int session = 0; session < 2; ++session) {
      int peer = ::accept(listener, nullptr, nullptr);
      if (peer < 0) { serverOk = false; return; }
      timeval timeout{};
      timeout.tv_sec = 3;
      ::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      const auto cmd3 = socketReadFrame(peer);
      if (cmd3.size() != 84 || getU32(cmd3.data() + 8) != 0x03) {
        serverOk = false; ::close(peer); return;
      }
      uint8_t noncePayload[32];
      memcpy(noncePayload, cmd3.data() + 16, 32);
      ecb(LOCAL_KEY, noncePayload, 32, MBEDTLS_AES_DECRYPT);
      const auto cmd4 = v34Cmd4Padded(true, false, false, false, false, noncePayload);
      if (!socketSendAll(peer, cmd4.data(), cmd4.size())) serverOk = false;
      const auto cmd5 = socketReadFrame(peer);
      const auto statusQuery = socketReadFrame(peer);
      if (cmd5.empty() || statusQuery.empty() ||
          getU32(statusQuery.data() + 8) != 0x10) serverOk = false;
      if (session == 1) {
        uint8_t key[16], digest[32];
        expectedSessionKeyV34(key, noncePayload);
        auto status = v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", key, false);
        hmac256(key, status.data(), status.size() - 36, digest);
        memcpy(status.data() + status.size() - 36, digest, 32);
        if (!socketSendAll(peer, status.data(), status.size())) serverOk = false;
        const auto heartbeat = socketReadFrame(peer);
        if (heartbeat.empty() || getU32(heartbeat.data() + 8) != 0x09) serverOk = false;
      }
      uint8_t one;
      if (::recv(peer, &one, 1, 0) != 0) serverOk = false;
      ::close(peer);
    }
  });
  TuyaLocalClient client;
  ClientEvents events;
  auto config = clientConfig();
  config.host = IPAddress(127, 0, 0, 1);
  config.port = ntohs(address.sin_port);
  config.connectionTimeoutMs = 500;
  config.commandResponseTimeoutMs = 100;
  config.heartbeatIntervalMs = 1000;
  config.statusReconciliationMs = 300000;
  config.reconnectMinimumMs = 250;
  config.maxBytesPerPoll = 32;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.context = &events;
  callbacks.onStatus = clientStatus;
  callbacks.onConnection = clientConnection;
  CHECK(client.begin(config, callbacks));
  const auto started = std::chrono::steady_clock::now();
  while (client.diagnostics().heartbeatFailures == 0 &&
         std::chrono::steady_clock::now() - started < std::chrono::seconds(4)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(events.status == 1 && events.connects == 2);
  CHECK(client.diagnostics().sessions == 2);
  CHECK(client.diagnostics().heartbeatFailures == 1);
  CHECK(client.diagnostics().connectionErrors >= 2);
  CHECK(!client.connected());
  CHECK(client.diagnostics().maxPollUs < 50000);
  client.end();
  server.join();
  ::close(listener);
  CHECK(serverOk.load());
}

static void testNativeOffPriority() {
  printf("-- native OFF priority over background status --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) == 0 &&
                         ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                       &addressLength) == 0 &&
                         ::listen(listener, 2) == 0;
  CHECK(listening);
  if (!listening) { ::close(listener); return; }
  std::atomic<bool> secondQuerySeen{false};
  std::atomic<bool> serverOk{true};
  std::thread server([&]() {
    for (int session = 0; session < 2; ++session) {
      int peer = ::accept(listener, nullptr, nullptr);
      if (peer < 0) { serverOk = false; return; }
      timeval timeout{};
      timeout.tv_sec = 3;
      ::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      const auto cmd3 = socketReadFrame(peer);
      if (cmd3.size() != 84) { serverOk = false; ::close(peer); return; }
      uint8_t noncePayload[32], key[16], digest[32];
      memcpy(noncePayload, cmd3.data() + 16, 32);
      ecb(LOCAL_KEY, noncePayload, 32, MBEDTLS_AES_DECRYPT);
      expectedSessionKeyV34(key, noncePayload);
      const auto cmd4 = v34Cmd4Padded(true, false, false, false, false, noncePayload);
      if (!socketSendAll(peer, cmd4.data(), cmd4.size())) serverOk = false;
      const auto cmd5 = socketReadFrame(peer);
      if (cmd5.empty() || getU32(cmd5.data() + 8) != 0x05) serverOk = false;
      const auto first = socketReadFrame(peer);
      if (first.empty() || getU32(first.data() + 8) != (session == 0 ? 0x10U : 0x0dU))
        serverOk = false;
      if (session == 0) {
        auto status = v34JsonFrame(3, 0x10, "{\"dps\":{\"1\":false}}", key, false);
        hmac256(key, status.data(), status.size() - 36, digest);
        memcpy(status.data() + status.size() - 36, digest, 32);
        if (!socketSendAll(peer, status.data(), status.size())) serverOk = false;
        const auto query = socketReadFrame(peer);
        if (query.empty() || getU32(query.data() + 8) != 0x10) serverOk = false;
        secondQuerySeen = true;
      } else {
        auto ack = v34EmptyAck(3, 0x0d);
        hmac256(key, ack.data(), ack.size() - 36, digest);
        memcpy(ack.data() + ack.size() - 36, digest, 32);
        if (!socketSendAll(peer, ack.data(), ack.size())) serverOk = false;
      }
      uint8_t one;
      if (::recv(peer, &one, 1, 0) != 0) serverOk = false;
      ::close(peer);
    }
  });
  TuyaLocalClient client;
  ClientEvents events;
  auto config = clientConfig();
  config.host = IPAddress(127, 0, 0, 1);
  config.port = ntohs(address.sin_port);
  config.connectionTimeoutMs = 500;
  config.commandResponseTimeoutMs = 500;
  config.maxBytesPerPoll = 32;
  TuyaLocalClient::Callbacks callbacks;
  callbacks.context = &events;
  callbacks.onStatus = clientStatus;
  callbacks.onCommand = clientCommand;
  CHECK(client.begin(config, callbacks));
  const auto started = std::chrono::steady_clock::now();
  while (events.status == 0 && std::chrono::steady_clock::now() - started <
         std::chrono::seconds(3)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(events.status == 1);
  CHECK(client.requestStatus());
  while (!secondQuerySeen && std::chrono::steady_clock::now() - started <
         std::chrono::seconds(3)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(secondQuerySeen.load());
  CHECK(client.requestBooleanDps("1", false, 1800000000));
  while (events.commands == 0 && std::chrono::steady_clock::now() - started <
         std::chrono::seconds(4)) {
    client.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(events.commands == 1 && events.lastAck && !events.lastRequested);
  CHECK(client.diagnostics().sessions == 2);
  CHECK(client.diagnostics().maxPollUs < 50000);
  client.end();
  server.join();
  ::close(listener);
  CHECK(serverOk.load());
}

static void testNativeUncertainControls() {
  printf("-- native disconnected ON/OFF result and no resend --\n");
  for (bool requestedOn : {true, false}) {
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    if (listener < 0) continue;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addressLength = sizeof(address);
    const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                   sizeof(address)) == 0 &&
                           ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                         &addressLength) == 0 &&
                           ::listen(listener, 1) == 0;
    CHECK(listening);
    if (!listening) { ::close(listener); continue; }
    std::atomic<int> controlFrames{0};
    std::atomic<bool> controlPayloadValid{false};
    std::thread server([&]() {
      int peer = ::accept(listener, nullptr, nullptr);
      if (peer < 0) return;
      timeval timeout{};
      timeout.tv_sec = 3;
      ::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      const auto cmd3 = socketReadFrame(peer);
      const auto cmd4 = v34Cmd4Padded(true);
      if (cmd3.size() == 84) socketSendAll(peer, cmd4.data(), cmd4.size());
      const auto cmd5 = socketReadFrame(peer);
      const auto query = socketReadFrame(peer);
      const auto status = v34StatusNoReturn("{\"dps\":{\"1\":false}}");
      if (!cmd5.empty() && !query.empty())
        socketSendAll(peer, status.data(), status.size());
      const auto control = socketReadFrame(peer);
      if (!control.empty() && getU32(control.data() + 8) == 0x0d) {
        ++controlFrames;
        uint8_t key[16];
        expectedSessionKeyV34(key);
        controlPayloadValid = decryptV34Frame(control, key).find(
            requestedOn ? "\"1\":true" : "\"1\":false") != std::string::npos;
      }
      ::close(peer);  // Command may have applied; no ACK is sent.
    });
    TuyaLocalClient client;
    ClientEvents events;
    auto config = clientConfig();
    config.host = IPAddress(127, 0, 0, 1);
    config.port = ntohs(address.sin_port);
    config.connectionTimeoutMs = 500;
    config.commandResponseTimeoutMs = 500;
    config.maxBytesPerPoll = 32;
    TuyaLocalClient::Callbacks callbacks;
    callbacks.context = &events;
    callbacks.onStatus = clientStatus;
    callbacks.onCommand = clientCommand;
    CHECK(client.begin(config, callbacks));
    const auto started = std::chrono::steady_clock::now();
    while (events.status == 0 && std::chrono::steady_clock::now() - started <
           std::chrono::seconds(3)) {
      client.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(events.status == 1);
    CHECK(client.requestBooleanDps("1", requestedOn, 1800000000));
    CHECK(!client.requestBooleanDps("1", requestedOn, 1800000000));
    while (events.commands == 0 && std::chrono::steady_clock::now() - started <
           std::chrono::seconds(4)) {
      client.poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(events.commands == 1 && !events.lastAck);
    bool ack = true;
    CHECK(client.takeCommandResult(ack) && !ack);
    CHECK(client.diagnostics().maxPollUs < 50000);
    client.end();
    server.join();
    ::close(listener);
    CHECK(controlFrames.load() == 1);
    CHECK(controlPayloadValid.load());
  }
}

static void testNativeBackoffWraparound() {
  printf("-- native refused connect and millis wraparound --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  CHECK(::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  CHECK(::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                      &addressLength) == 0);
  ::close(listener);  // A local, known-closed port; no LAN packet is sent.
  TuyaLocalClient client;
  auto config = clientConfig();
  config.host = IPAddress(127, 0, 0, 1);
  config.port = ntohs(address.sin_port);
  config.reconnectMinimumMs = 250;
  config.reconnectMaximumMs = 500;
  useFakeMillis() = true;
  fakeMillis() = UINT32_MAX - 100;
  CHECK(client.begin(config));
  for (int i = 0; i < 10 && client.diagnostics().connectionErrors == 0; ++i)
    client.poll();
  CHECK(client.diagnostics().connectionErrors == 1);
  CHECK(client.diagnostics().scheduledReconnectDelayMs == 250);
  fakeMillis() += 249;
  CHECK(client.poll() == TuyaLocalClient::PollResult::NoFrame);
  CHECK(client.diagnostics().connectionErrors == 1);
  fakeMillis() += 1;  // Wrap and reach the retry deadline exactly.
  for (int i = 0; i < 10 && client.diagnostics().connectionErrors == 1; ++i)
    client.poll();
  CHECK(client.diagnostics().connectionErrors == 2);
  CHECK(client.diagnostics().scheduledReconnectDelayMs == 500);
  client.end();
  useFakeMillis() = false;
}

static void testNativeTransportBackpressure() {
  printf("-- native partial send, backpressure, and close --\n");
  int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(listener >= 0);
  if (listener < 0) return;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t addressLength = sizeof(address);
  const bool listening = ::bind(listener, reinterpret_cast<sockaddr*>(&address),
                                 sizeof(address)) == 0 &&
                         ::getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                                       &addressLength) == 0 &&
                         ::listen(listener, 1) == 0;
  CHECK(listening);
  if (!listening) { ::close(listener); return; }
  TuyaSocketTransport transport;
  CHECK(transport.start(IPAddress(127, 0, 0, 1), ntohs(address.sin_port)));
  int peer = ::accept(listener, nullptr, nullptr);
  CHECK(peer >= 0);
  if (peer < 0) { transport.stop(); ::close(listener); return; }
  for (int i = 0; i < 100 && transport.progressConnect() ==
       TuyaSocketTransport::State::Connecting; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(transport.state() == TuyaSocketTransport::State::Open);
  uint8_t frame[64];
  memset(frame, 0xA5, sizeof(frame));
  CHECK(transport.write(frame, sizeof(frame)) == sizeof(frame));
  CHECK(transport.serviceTx(0) == 0 && transport.txPending());
  for (int i = 0; i < 10 && transport.txPending(); ++i)
    CHECK(transport.serviceTx(7) >= 0);
  CHECK(!transport.txPending());
  uint8_t received[64];
  CHECK(socketReadExact(peer, received, sizeof(received)));
  CHECK(memcmp(frame, received, sizeof(frame)) == 0);

  int sendBuffer = 4096;
  ::setsockopt(transport.nativeFd(), SOL_SOCKET, SO_SNDBUF,
               &sendBuffer, sizeof(sendBuffer));
  uint8_t filler[4096] = {};
  bool saturated = false;
  for (size_t i = 0; i < 4096; ++i) {
    const int n = ::send(transport.nativeFd(), filler, sizeof(filler), MSG_DONTWAIT);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      saturated = true;
      break;
    }
    if (n <= 0) break;
  }
  CHECK(saturated);
  CHECK(transport.write(frame, sizeof(frame)) == sizeof(frame));
  const int blocked = transport.serviceTx(sizeof(frame));
  CHECK(blocked == 0 && transport.txPending());
  uint8_t drain[8192];
  for (int i = 0; i < 2000 && transport.txPending(); ++i) {
    ::recv(peer, drain, sizeof(drain), MSG_DONTWAIT);
    CHECK(transport.serviceTx(sizeof(frame)) >= 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(!transport.txPending());
  transport.stop();
  CHECK(transport.nativeFd() == -1 && transport.state() ==
        TuyaSocketTransport::State::Closed);
  ::close(peer);
  ::close(listener);
}

int main() {
  testV34();

  {  // heartbeat frame inspection needs the tx from a fresh session
    uint8_t sessionKey[16];
    expectedSessionKeyV34(sessionKey);
    MockClient mock;
    LynkTuyaDevice<TUYA_V34> dev(IPAddress(192, 168, 1, 50), (const char*)LOCAL_KEY, mock);
    dev.setTimeout(300);
    mock.preload(v34Cmd4Response());
    mock.preload(v34EmptyAck(3, 0x09));
    CHECK(dev.heartbeat());
    testV34Heartbeat(splitV34Frames(mock.tx), sessionKey);
  }

  testV34Fragmented();
  testV35();
  testV35Fragmented();
  testShortKey();
  testMalformedLength();
  testUnalignedPayload();
  testAuthenticatedFailures();
  testControlAcknowledgements();
  testV34ResponseVariantsAndDiagnostics();
  testV34PaddedHandshake();
  testProxyContentionModel();
  testPersistentPushAndHealth();
  testIncrementalPushFrames();
  testPollBlockingBoundary();
  testTuyaLocalClient();
  testTuyaLocalClientFailures();
  testTuyaLocalConfiguration();
  testNativeCooperativeSocket();
  testNativeMissingHandshake();
  testNativeRecoveryAndHeartbeat();
  testNativeOffPriority();
  testNativeUncertainControls();
  testNativeBackoffWraparound();
  testNativeTransportBackpressure();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
