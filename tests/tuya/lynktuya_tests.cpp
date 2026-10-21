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

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"

SerialStub Serial;

#include <LynkTuya.h>
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

  int connect(IPAddress, uint16_t) override {
    if (!allowConnect) return 0;
    isConn = true;
    return 1;
  }
  size_t write(const uint8_t* buf, size_t size) override {
    tx.insert(tx.end(), buf, buf + size);
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

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
