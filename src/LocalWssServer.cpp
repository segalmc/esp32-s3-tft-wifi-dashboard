#include "LocalWssServer.h"
#include "LocalWssPolicy.h"
#include "LocalWssClockPolicy.h"
#if TFT_HARDWARE_ENABLED
#include <esp_system.h>
#include <esp_event.h>
#include <lwip/sockets.h>
#include <mbedtls/md.h>
#include <mbedtls/x509_crt.h>
#include <time.h>
#include <string.h>

#if __has_include("LocalWssConfig.h")
#include "LocalWssConfig.h"
#endif
#ifndef LOCAL_WSS_HOSTNAME
#define LOCAL_WSS_HOSTNAME ""
#define LOCAL_WSS_CERT_PEM ""
#define LOCAL_WSS_KEY_PEM ""
#define LOCAL_WSS_CA_PEM ""
#define LOCAL_WSS_DEVICE_ID ""
#define LOCAL_WSS_INSTALLATION_ID ""
#define LOCAL_WSS_SECRET_HEX ""
#endif
#ifndef LOCAL_WSS_CA_PEM
#define LOCAL_WSS_CA_PEM ""
#endif
#ifndef LOCAL_WSS_SECOND_SECRET_HEX
#define LOCAL_WSS_SECOND_SECRET_HEX ""
#endif
#ifndef LOCAL_WSS_PORT
#define LOCAL_WSS_PORT 9443
#endif

namespace
{
  constexpr uint32_t kAuthTimeoutMs = 10000;
  constexpr uint32_t kPingIntervalMs = 20000;
  constexpr uint32_t kPongTimeoutMs = 10000;
  constexpr uint32_t kIdleTimeoutMs = 60000;
  constexpr size_t kMaxFrame = 2048;
  constexpr char kHex[] = "0123456789abcdef";

  bool textField(JsonVariantConst value, size_t max_len)
  {
    return value.is<const char *>() && value.as<const char *>() &&
           strlen(value.as<const char *>()) > 0 &&
           strlen(value.as<const char *>()) <= max_len;
  }
  bool same(JsonVariantConst value, const char *expected)
  {
    return value.is<const char *>() && strcmp(value.as<const char *>(), expected) == 0;
  }
  bool uuid(const char *value)
  {
    if (!value || strlen(value) != 36)
      return false;
    for (size_t i = 0; i < 36; ++i)
    {
      if (i == 8 || i == 13 || i == 18 || i == 23)
      {
        if (value[i] != '-')
          return false;
      }
      else if (!isxdigit(static_cast<unsigned char>(value[i])))
        return false;
    }
    return true;
  }
  bool validHostname(const char *value)
  {
    if (!value)
      return false;
    const size_t length = strnlen(value, 129);
    if (length < 4 || length > 128)
      return false;
    bool dot = false, letter = false;
    size_t label = 0;
    for (size_t i = 0; i < length; ++i)
    {
      const unsigned char c = value[i];
      if (c == '.')
      {
        if (!label || value[i - 1] == '-')
          return false;
        dot = true;
        label = 0;
      }
      else
      {
        if (!isalnum(c) && c != '-')
          return false;
        if (!label && c == '-')
          return false;
        letter |= isalpha(c) != 0;
        if (++label > 63)
          return false;
      }
    }
    return dot && letter && label && value[length - 1] != '-';
  }
  int nibble(char c)
  {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    return -1;
  }
  bool hexBytes(const char *input, uint8_t *bytes, size_t count)
  {
    if (!input || strlen(input) != count * 2)
      return false;
    for (size_t i = 0; i < count; ++i)
    {
      int a = nibble(input[i * 2]), b = nibble(input[i * 2 + 1]);
      if (a < 0 || b < 0)
        return false;
      bytes[i] = static_cast<uint8_t>((a << 4) | b);
    }
    return true;
  }
  time_t utcEpoch(struct tm parts)
  {
    // Calendar-to-epoch conversion independent of the display's Israel TZ.
    int year = parts.tm_year + 1900;
    const unsigned month = parts.tm_mon + 1, day = parts.tm_mday;
    if (year < 2020 || year > 2100 || month < 1 || month > 12 ||
        day < 1 || day > 31 || parts.tm_hour > 23 || parts.tm_min > 59 ||
        parts.tm_sec > 59)
      return static_cast<time_t>(-1);
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned yoe_days = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + yoe_days - 719468;
    return static_cast<time_t>(days * 86400 + parts.tm_hour * 3600 +
                               parts.tm_min * 60 + parts.tm_sec);
  }
  void randomHex(char *output, size_t bytes)
  {
    uint8_t random[32];
    esp_fill_random(random, bytes);
    for (size_t i = 0; i < bytes; ++i)
    {
      output[2 * i] = kHex[random[i] >> 4];
      output[2 * i + 1] = kHex[random[i] & 15];
    }
    output[2 * bytes] = 0;
    memset(random, 0, sizeof(random));
  }
  bool utc(const char *input, time_t *result)
  {
    if (!input || strlen(input) < 20 || strlen(input) > 31)
      return false;
    struct tm parts = {};
    char *end = strptime(input, "%Y-%m-%dT%H:%M:%S", &parts);
    if (!end)
      return false;
    if (*end == '.')
    {
      ++end;
      if (!isdigit(static_cast<unsigned char>(*end)))
        return false;
      while (isdigit(static_cast<unsigned char>(*end)))
        ++end;
    }
    if (*end != 'Z' || end[1])
      return false;
    const time_t parsed = utcEpoch(parts);
    if (parsed == static_cast<time_t>(-1))
      return false;
    char canonical[21];
    struct tm check;
    gmtime_r(&parsed, &check);
    strftime(canonical, sizeof(canonical), "%Y-%m-%dT%H:%M:%SZ", &check);
    if (strncmp(input, canonical, 19) != 0)
      return false;
    *result = parsed;
    return true;
  }
  void wireTime(time_t value, char *output, size_t length)
  {
    struct tm parts;
    gmtime_r(&value, &parts);
    strftime(output, length, "%Y-%m-%dT%H:%M:%SZ", &parts);
  }
  bool duplicateKeys(const char *data, size_t length)
  {
    // ArduinoJson keeps the last duplicate; reject duplicates before parsing.
    uint32_t seen[4][20] = {};
    uint8_t counts[4] = {};
    int depth = -1;
    bool quoted = false, escaped = false, key_escaped = false;
    size_t start = 0;
    for (size_t i = 0; i < length; ++i)
    {
      const char c = data[i];
      if (quoted)
      {
        if (escaped)
        {
          escaped = false;
          continue;
        }
        if (c == '\\')
        {
          escaped = true;
          key_escaped = true;
          continue;
        }
        if (c == '"')
        {
          quoted = false;
          size_t next = i + 1;
          while (next < length && isspace(static_cast<unsigned char>(data[next])))
            ++next;
          if (next < length && data[next] == ':' && depth >= 0)
          {
            if (key_escaped)
              return false;
            uint32_t hash = 2166136261U;
            for (size_t j = start; j < i; ++j)
              hash = (hash ^ static_cast<uint8_t>(data[j])) * 16777619U;
            if (counts[depth] >= 20)
              return false;
            for (uint8_t j = 0; j < counts[depth]; ++j)
              if (seen[depth][j] == hash)
                return false;
            seen[depth][counts[depth]++] = hash;
          }
        }
        continue;
      }
      if (c == '"')
      {
        quoted = true;
        key_escaped = false;
        start = i + 1;
      }
      else if (c == '{')
      {
        if (++depth >= 4)
          return false;
        counts[depth] = 0;
      }
      else if (c == '}')
      {
        if (--depth < -1)
          return false;
      }
    }
    return !quoted && depth == -1;
  }
  bool validUtf8(const uint8_t *data, size_t length)
  {
    for (size_t i = 0; i < length;)
    {
      const uint8_t first = data[i++];
      if (first < 0x80)
        continue;
      unsigned trailing = 0;
      uint32_t codepoint = 0, minimum = 0;
      if (first >= 0xC2 && first <= 0xDF)
      {
        trailing = 1;
        codepoint = first & 0x1F;
        minimum = 0x80;
      }
      else if (first >= 0xE0 && first <= 0xEF)
      {
        trailing = 2;
        codepoint = first & 0x0F;
        minimum = 0x800;
      }
      else if (first >= 0xF0 && first <= 0xF4)
      {
        trailing = 3;
        codepoint = first & 0x07;
        minimum = 0x10000;
      }
      else
        return false;
      if (length - i < trailing)
        return false;
      for (unsigned j = 0; j < trailing; ++j)
      {
        const uint8_t byte = data[i++];
        if ((byte & 0xC0) != 0x80)
          return false;
        codepoint = (codepoint << 6) | (byte & 0x3F);
      }
      if (codepoint < minimum || codepoint > 0x10FFFF ||
          (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return false;
    }
    return true;
  }
  bool appendPart(uint8_t *output, size_t *used, const char *value)
  {
    const size_t len = strlen(value);
    if (len > 65535 || *used + len + 2 > 512)
      return false;
    output[(*used)++] = static_cast<uint8_t>(len >> 8);
    output[(*used)++] = static_cast<uint8_t>(len);
    memcpy(output + *used, value, len);
    *used += len;
    return true;
  }
  bool verifyMac(const char *secret_hex, JsonObjectConst f)
  {
    uint8_t key[32] = {}, expected[32] = {}, actual[32] = {}, transcript[512] = {};
    if (!hexBytes(secret_hex, key, 32) || !hexBytes(f["mac"], actual, 32))
      return false;
    const char *fields[] = {"HWP-WS-V1-AUTH", f["link"], f["deviceRole"],
                            f["installationId"], f["deviceId"], f["bootId"], f["deviceSessionId"],
                            f["connectionSessionId"], f["serverNonce"], f["clientNonce"], f["expiresAt"]};
    size_t used = 0;
    for (const char *field : fields)
      if (!field || !appendPart(transcript, &used, field))
      {
        memset(key, 0, sizeof(key));
        return false;
      }
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok = info && mbedtls_md_hmac(info, key, sizeof(key), transcript, used, expected) == 0;
    uint8_t difference = 0;
    for (size_t i = 0; i < 32; ++i)
      difference |= expected[i] ^ actual[i];
    memset(key, 0, sizeof(key));
    memset(transcript, 0, sizeof(transcript));
    memset(expected, 0, sizeof(expected));
    memset(actual, 0, sizeof(actual));
    return ok && difference == 0;
  }
  struct CertificateWindow
  {
    time_t start = 0;
    time_t end = 0;
  };
  time_t x509Epoch(const mbedtls_x509_time &value)
  {
    struct tm parts = {};
    parts.tm_year = value.year - 1900;
    parts.tm_mon = value.mon - 1;
    parts.tm_mday = value.day;
    parts.tm_hour = value.hour;
    parts.tm_min = value.min;
    parts.tm_sec = value.sec;
    return utcEpoch(parts);
  }
  CertificateWindow certificateWindow(time_t now)
  {
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt ca;
    mbedtls_x509_crt_init(&certificate);
    mbedtls_x509_crt_init(&ca);
    const int parsed = mbedtls_x509_crt_parse(&certificate,
                                              reinterpret_cast<const unsigned char *>(LOCAL_WSS_CERT_PEM),
                                              strlen(LOCAL_WSS_CERT_PEM) + 1);
    const int ca_parsed = mbedtls_x509_crt_parse(&ca,
                                                 reinterpret_cast<const unsigned char *>(LOCAL_WSS_CA_PEM),
                                                 strlen(LOCAL_WSS_CA_PEM) + 1);
    CertificateWindow window;
    uint32_t flags = 0;
    if (parsed == 0 && ca_parsed == 0 &&
        mbedtls_x509_crt_verify(&certificate, &ca, nullptr, LOCAL_WSS_HOSTNAME,
                                &flags, nullptr, nullptr) == 0)
    {
      window.start = max(x509Epoch(certificate.valid_from), x509Epoch(ca.valid_from));
      window.end = min(x509Epoch(certificate.valid_to), x509Epoch(ca.valid_to));
      if (window.start > now || now >= window.end)
        window = {};
    }
    mbedtls_x509_crt_free(&certificate);
    mbedtls_x509_crt_free(&ca);
    return window;
  }
} // namespace

void LocalWssServer::begin(NatureNetworkManager *network)
{
  network_ = network;
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_ || xTaskCreate(taskEntry, "local-wss", 6144, this, 1, &task_) != pdPASS)
    Serial.println("Local WSS: task unavailable");
}
void LocalWssServer::tlsError(void *context, esp_event_base_t, int32_t event_id, void *)
{
  auto *self = static_cast<LocalWssServer *>(context);
  const uint32_t count = self->tls_failures_.fetch_add(1, std::memory_order_relaxed) + 1;
  Serial.printf("Local WSS: TLS error event=%ld count=%lu at_ms=%lu\n",
                static_cast<long>(event_id), static_cast<unsigned long>(count),
                static_cast<unsigned long>(millis()));
}
void LocalWssServer::taskEntry(void *context) { static_cast<LocalWssServer *>(context)->run(); }
bool LocalWssServer::configured() const
{
  uint8_t secret[32];
  const bool valid = validHostname(LOCAL_WSS_HOSTNAME) &&
                     strlen(LOCAL_WSS_CERT_PEM) > 0 &&
                     strlen(LOCAL_WSS_KEY_PEM) > 0 && strlen(LOCAL_WSS_CA_PEM) > 0 &&
                     uuid(LOCAL_WSS_DEVICE_ID) &&
                     uuid(LOCAL_WSS_INSTALLATION_ID) && hexBytes(LOCAL_WSS_SECRET_HEX, secret, 32) &&
                     (!LOCAL_WSS_SECOND_SECRET_HEX[0] || hexBytes(LOCAL_WSS_SECOND_SECRET_HEX, secret, 32)) &&
                     LOCAL_WSS_PORT >= 1024 && LOCAL_WSS_PORT <= 65535;
  memset(secret, 0, sizeof(secret));
  return valid;
}
bool LocalWssServer::allowed(IPAddress *ip) const
{
  if (!network_)
    return false;
  const auto state = network_->snapshot();
  if (state.state != NatureNetworkManager::State::Online || state.ap_active ||
      !state.clock_synced || !clock_integrity_.load(std::memory_order_acquire) ||
      WiFi.status() != WL_CONNECTED)
    return false;
  const IPAddress address = WiFi.localIP();
  if (address == IPAddress(0, 0, 0, 0) || address == IPAddress(192, 168, 4, 1) ||
      time(nullptr) < 1700000000)
    return false;
  if (ip)
    *ip = address;
  return true;
}
LocalWssServer::Telemetry LocalWssServer::telemetry() const
{
  Telemetry copy;
  if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE)
  {
    snapshot_misses_.fetch_add(1, std::memory_order_relaxed);
    return copy;
  }
  copy = telemetry_;
  copy.snapshot_ok = true;
  copy.connected = phase_ == Phase::Ready;
  copy.stale = local_wss_policy::stale(copy.connected, copy.valid,
                                       copy.quality, millis(), copy.received_ms);
  xSemaphoreGive(mutex_);
  return copy;
}
void LocalWssServer::diagnostics() const
{
  if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    return;
  const unsigned clients = phase_ == Phase::Empty ? 0U : 1U;
  const uint32_t auth_failures = auth_failures_, resets = connection_resets_;
  const uint32_t accepted = accepted_, rejected_telemetry = rejected_telemetry_;
  const uint32_t rejected = rejected_;
  const uint32_t age_ms = telemetry_.valid ? millis() - telemetry_.received_ms : 0;
  const uint32_t http_stack = http_stack_free_bytes_, mutex_misses = mutex_misses_;
  xSemaphoreGive(mutex_);
  Serial.printf("Local WSS: clients=%u auth_fail=%lu tls_error=%lu resets=%lu telemetry_accepted=%lu telemetry_rejected=%lu app_rejected=%lu age_ms=%lu heap=%lu min_heap=%lu psram=%lu manager_stack_free_B=%lu http_stack_free_B=%lu mutex_miss=%lu snapshot_miss=%lu task_lag_max_ms=%lu ping_send_error=%lu\n",
                clients, (unsigned long)auth_failures,
                (unsigned long)tls_failures_.load(std::memory_order_relaxed),
                (unsigned long)resets,
                (unsigned long)accepted, (unsigned long)rejected_telemetry,
                (unsigned long)rejected,
                (unsigned long)age_ms,
                (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap(),
                (unsigned long)ESP.getFreePsram(),
                task_ ? (unsigned long)uxTaskGetStackHighWaterMark(task_) : 0UL,
                (unsigned long)http_stack, (unsigned long)mutex_misses,
                (unsigned long)snapshot_misses_.load(std::memory_order_relaxed),
                (unsigned long)task_lag_max_ms_.load(std::memory_order_relaxed),
                (unsigned long)ping_send_errors_.load(std::memory_order_relaxed));
}
void LocalWssServer::clearSession()
{
  client_fd_ = -1;
  phase_ = Phase::Empty;
  phase_ms_ = 0;
  last_frame_ms_ = 0;
  last_ping_ms_ = 0;
  awaiting_pong_ = false;
  memset(boot_id_, 0, sizeof(boot_id_));
  memset(device_session_id_, 0, sizeof(device_session_id_));
  memset(connection_session_id_, 0, sizeof(connection_session_id_));
  memset(server_nonce_, 0, sizeof(server_nonce_));
  memset(expires_at_, 0, sizeof(expires_at_));
  telemetry_.connected = false;
  telemetry_.stale = true;
}
void LocalWssServer::closeClient(const char *reason)
{
  Serial.printf("Local WSS: close reason=%s phase=%u fd=%d since_frame_ms=%lu since_ping_ms=%lu awaiting_pong=%u heap=%lu\n",
                reason, static_cast<unsigned>(phase_), client_fd_,
                static_cast<unsigned long>(millis() - last_frame_ms_),
                static_cast<unsigned long>(millis() - last_ping_ms_), awaiting_pong_,
                static_cast<unsigned long>(ESP.getFreeHeap()));
  if (server_ && client_fd_ >= 0)
    httpd_sess_trigger_close(server_, client_fd_);
  ++connection_resets_;
  clearSession();
}
void LocalWssServer::pingWork(void *context)
{
  auto *self = static_cast<LocalWssServer *>(context);
  if (!self->mutex_ || xSemaphoreTake(self->mutex_, pdMS_TO_TICKS(20)) != pdTRUE)
    return;
  const int fd = self->client_fd_;
  const bool send = self->server_ && fd >= 0 &&
                    self->phase_ == Phase::Ready && self->awaiting_pong_;
  xSemaphoreGive(self->mutex_);
  if (!send)
    return;
  httpd_ws_frame_t ping = {};
  ping.type = HTTPD_WS_TYPE_PING;
  const esp_err_t result = httpd_ws_send_frame_async(self->server_, fd, &ping);
  if (result != ESP_OK)
  {
    ++self->ping_send_errors_;
    Serial.printf("Local WSS: ping send error=%ld fd=%d\n", static_cast<long>(result), fd);
  }
}
void LocalWssServer::run()
{
  if (!configured())
    Serial.println("Local WSS: disabled; local TLS or device configuration missing/invalid");
  uint32_t last_diag = millis();
  uint32_t cert_retry_at = 0;
  CertificateWindow certificate;
  time_t last_utc = 0;
  uint32_t last_clock_ms = 0;
  bool had_clock_sync = false;
  bool awaiting_clock_resync = false;
  uint32_t sync_generation_at_jump = 0;
  uint32_t last_resync_request_ms = 0;
  bool tls_events_registered = false;
  uint32_t previous_cycle_ms = 0;
  for (;;)
  {
    IPAddress ip;
    const time_t now_utc = time(nullptr);
    const uint32_t now_ms = millis();
    if (previous_cycle_ms && now_ms - previous_cycle_ms > task_lag_max_ms_.load(std::memory_order_relaxed))
      task_lag_max_ms_.store(now_ms - previous_cycle_ms, std::memory_order_relaxed);
    previous_cycle_ms = now_ms;
    const auto network_snapshot = network_->snapshot();
    const bool synced = network_snapshot.clock_synced;
    if (!synced)
    {
      had_clock_sync = false;
      last_utc = 0;
      if (!awaiting_clock_resync)
        clock_integrity_.store(true, std::memory_order_release);
    }
    else if (awaiting_clock_resync)
    {
      if (local_wss_clock_policy::recovered(
              synced, network_snapshot.clock_sync_generation,
              sync_generation_at_jump, now_utc))
      {
        awaiting_clock_resync = false;
        clock_integrity_.store(true, std::memory_order_release);
        had_clock_sync = true;
        last_utc = now_utc;
        last_clock_ms = now_ms;
        Serial.println("Local WSS: clock integrity restored after fresh SNTP sync");
      }
    }
    else if (!had_clock_sync)
    {
      had_clock_sync = true;
      last_utc = now_utc;
      last_clock_ms = now_ms;
    }
    else if (last_utc)
    {
      const int64_t wall_delta = static_cast<int64_t>(now_utc - last_utc);
      if (local_wss_clock_policy::jumped(wall_delta, now_ms - last_clock_ms))
      {
        clock_integrity_.store(false, std::memory_order_release);
        awaiting_clock_resync = true;
        sync_generation_at_jump = network_snapshot.clock_sync_generation;
        last_resync_request_ms = now_ms;
        network_->requestClockResync();
        Serial.println("Local WSS: clock jump; listener closed pending fresh SNTP sync");
      }
      last_utc = now_utc;
      last_clock_ms = now_ms;
    }
    if (awaiting_clock_resync &&
        network_snapshot.state == NatureNetworkManager::State::Online &&
        local_wss_clock_policy::retry_due(now_ms, last_resync_request_ms))
    {
      last_resync_request_ms = now_ms;
      network_->requestClockResync();
      Serial.println("Local WSS: retrying trusted clock synchronization");
    }
    const bool network_ready = configured() && allowed(&ip);
    if (network_ready && (now_utc < certificate.start || now_utc >= certificate.end) &&
        static_cast<int32_t>(millis() - cert_retry_at) >= 0)
    {
      certificate = certificateWindow(now_utc);
      cert_retry_at = millis() + 30000;
    }
    const bool ready = network_ready && certificate.start <= now_utc &&
                       now_utc < certificate.end;
    static uint32_t last_gate_log = 0;

    if (now_ms - last_gate_log >= 30000)
    {
      last_gate_log = now_ms;

      Serial.printf(
          "WSS DIAG: configured=%d network_ready=%d "
          "wifi=%d clock_synced=%d integrity=%d "
          "cert_valid=%d server_running=%d\n",
          configured(),
          network_ready,
          WiFi.status() == WL_CONNECTED,
          network_snapshot.clock_synced,
          clock_integrity_.load(),
          certificate.start > 0 &&
              certificate.start <= now_utc &&
              now_utc < certificate.end,
          server_ != nullptr);
    }
    if (local_wss_clock_policy::stop_listener(server_ != nullptr, ready, ip == sta_ip_))
    {
      const auto net = network_->snapshot();
      Serial.printf("Local WSS: gate ready=%u network=%u state=%u ap=%u clock=%u clock_integrity=%u wifi_status=%u ip_changed=%u cert_valid=%u\n",
                    ready, network_ready, static_cast<unsigned>(net.state), net.ap_active,
                    net.clock_synced, clock_integrity_.load(std::memory_order_relaxed),
                    static_cast<unsigned>(WiFi.status()), ip != sta_ip_,
                    certificate.start <= now_utc && now_utc < certificate.end);
      if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE)
      {
        closeClient(!network_ready ? "network-gate" : ip != sta_ip_ ? "ip-change"
                                                                    : "clock-or-certificate");
        xSemaphoreGive(mutex_);
      }
      httpd_ssl_stop(server_);
      server_ = nullptr;
      if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(1000)) == pdTRUE)
      {
        clearSession();
        xSemaphoreGive(mutex_);
      }
      Serial.println("Local WSS: stopped on network, address or clock change");
    }
    if (local_wss_clock_policy::start_listener(server_ != nullptr, ready))
    {
      if (!tls_events_registered)
        tls_events_registered = esp_event_handler_register(
                                    ESP_HTTPS_SERVER_EVENT, HTTPS_SERVER_EVENT_ERROR, tlsError, this) == ESP_OK;
      httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
      config.port_secure = LOCAL_WSS_PORT;
      config.servercert = reinterpret_cast<const uint8_t *>(LOCAL_WSS_CERT_PEM);
      config.servercert_len = strlen(LOCAL_WSS_CERT_PEM) + 1;
      config.prvtkey_pem = reinterpret_cast<const uint8_t *>(LOCAL_WSS_KEY_PEM);
      config.prvtkey_len = strlen(LOCAL_WSS_KEY_PEM) + 1;
      config.httpd.ctrl_port = 32770;
      config.httpd.stack_size = 20480;
      config.httpd.task_priority = tskIDLE_PRIORITY + 1;
      config.httpd.core_id = 0; // TFT Arduino loop runs on core 1.
      config.httpd.max_open_sockets = 2;
      config.httpd.max_uri_handlers = 1;
      config.httpd.lru_purge_enable = false;
      config.httpd.recv_wait_timeout = 2;
      config.httpd.send_wait_timeout = 2;
      config.tls_handshake_timeout_ms = 5000;
      httpd_handle_t created = nullptr;
      if (httpd_ssl_start(&created, &config) == ESP_OK)
      {
        sta_ip_ = ip;
        httpd_uri_t route = {};
        route.uri = "/hwp/ws/v1/local";
        route.method = HTTP_GET;
        route.handler = wsEntry;
        route.user_ctx = this;
        route.is_websocket = true;
        route.handle_ws_control_frames = true;
        if (httpd_register_uri_handler(created, &route) == ESP_OK)
        {
          server_ = created;
          Serial.printf("Local WSS: listening on STA port %u\n", LOCAL_WSS_PORT);
        }
        else
        {
          httpd_ssl_stop(created);
          ++tls_failures_;
        }
      }
      else
      {
        ++tls_failures_;
        Serial.println("WSS DIAG: httpd_ssl_start failed");
        vTaskDelay(pdMS_TO_TICKS(5000));
      }
    }
    if (server_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE)
    {
      const uint32_t now = millis();
      if (client_fd_ >= 0 && httpd_ws_get_fd_info(server_, client_fd_) != HTTPD_WS_CLIENT_WEBSOCKET)
      {
        Serial.printf("Local WSS: socket closed fd=%d phase=%u\n", client_fd_, static_cast<unsigned>(phase_));
        clearSession();
      }
      if (phase_ == Phase::Challenge && now - phase_ms_ >= kAuthTimeoutMs)
      {
        ++auth_failures_;
        closeClient("auth-timeout");
      }
      else if (phase_ == Phase::Ready)
      {
        if (awaiting_pong_ && now - last_ping_ms_ >= kPongTimeoutMs)
          closeClient("pong-timeout");
        else if (now - last_frame_ms_ >= kIdleTimeoutMs)
          closeClient("frame-idle-timeout");
        else if (!awaiting_pong_ && now - last_ping_ms_ >= kPingIntervalMs)
        {
          awaiting_pong_ = true;
          last_ping_ms_ = now;
          const esp_err_t queued = httpd_queue_work(server_, pingWork, this);
          if (queued != ESP_OK)
          {
            awaiting_pong_ = false;
            ++ping_send_errors_;
            Serial.printf("Local WSS: ping queue error=%ld\n", static_cast<long>(queued));
          }
        }
      }
      xSemaphoreGive(mutex_);
    }
    else if (server_)
      ++mutex_misses_;
    if (millis() - last_diag >= 30000)
    {
      diagnostics();
      last_diag = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
esp_err_t LocalWssServer::wsEntry(httpd_req_t *req)
{
  return static_cast<LocalWssServer *>(req->user_ctx)->onFrame(req);
}
void LocalWssServer::reject(httpd_req_t *req)
{
  ++rejected_;
  const int fd = httpd_req_to_sockfd(req);
  Serial.printf("Local WSS: reject phase=%u fd=%d auth_fail=%lu telemetry_reject=%lu\n",
                static_cast<unsigned>(phase_), fd, static_cast<unsigned long>(auth_failures_),
                static_cast<unsigned long>(rejected_telemetry_));
  if (server_)
    httpd_sess_trigger_close(server_, fd);
  if (client_fd_ == fd)
    clearSession();
}
bool LocalWssServer::sendJson(httpd_req_t *req, const char *data)
{
  httpd_ws_frame_t frame = {};
  frame.type = HTTPD_WS_TYPE_TEXT;
  frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(data));
  frame.len = strlen(data);
  return httpd_ws_send_frame(req, &frame) == ESP_OK;
}
esp_err_t LocalWssServer::onFrame(httpd_req_t *req)
{
  IPAddress ip;
  if (!server_ || !allowed(&ip) || ip != sta_ip_)
  {
    Serial.println("Local WSS: rejected frame at network gate");
    return ESP_FAIL;
  }
  struct sockaddr_storage local = {};
  socklen_t size = sizeof(local);
  const int fd = httpd_req_to_sockfd(req);
  const int socket_result = getsockname(fd, reinterpret_cast<sockaddr *>(&local), &size);
  const uint8_t *octets = nullptr;
  if (socket_result == 0 && local.ss_family == AF_INET)
  {
    const auto *address = reinterpret_cast<const sockaddr_in *>(&local);
    octets = reinterpret_cast<const uint8_t *>(&address->sin_addr.s_addr);
  }
  else if (socket_result == 0 && local.ss_family == AF_INET6)
  {
    const auto *address = reinterpret_cast<const sockaddr_in6 *>(&local);
    if (IN6_IS_ADDR_V4MAPPED(&address->sin6_addr))
      octets = &address->sin6_addr.s6_addr[12];
  }
  const bool sta_socket = octets && octets[0] == ip[0] && octets[1] == ip[1] &&
                          octets[2] == ip[2] && octets[3] == ip[3];
  if (!sta_socket)
  {
    Serial.println("Local WSS: rejected frame at STA socket gate");
    return ESP_FAIL;
  }
  if (req->method == HTTP_GET)
    return ESP_OK;
  uint8_t payload[kMaxFrame + 1] = {};
  httpd_ws_frame_t frame = {};
  frame.payload = payload;
  const esp_err_t receive_error = httpd_ws_recv_frame(req, &frame, kMaxFrame);
  if (receive_error != ESP_OK)
  {
    Serial.printf("Local WSS: rejected frame receive error=%ld\n", static_cast<long>(receive_error));
    return ESP_FAIL;
  }
  if (!frame.final || frame.len > kMaxFrame ||
      (frame.type != HTTPD_WS_TYPE_TEXT && frame.type != HTTPD_WS_TYPE_PING &&
       frame.type != HTTPD_WS_TYPE_PONG))
  {
    Serial.printf("Local WSS: rejected frame shape type=%u final=%u len=%u\n",
                  static_cast<unsigned>(frame.type), frame.final, static_cast<unsigned>(frame.len));
    return ESP_FAIL;
  }
  if (frame.type == HTTPD_WS_TYPE_PING)
  {
    Serial.println("Local WSS: client ping received");
    frame.type = HTTPD_WS_TYPE_PONG;
    return httpd_ws_send_frame(req, &frame);
  }
  if (frame.type == HTTPD_WS_TYPE_PONG)
  {
    Serial.println("Local WSS: client pong received");
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE)
    {
      if (fd == client_fd_ && phase_ == Phase::Ready)
      {
        awaiting_pong_ = false;
        last_frame_ms_ = millis();
      }
      xSemaphoreGive(mutex_);
    }
    return ESP_OK;
  }
  if (!frame.len || memchr(payload, 0, frame.len) || !validUtf8(payload, frame.len) ||
      !duplicateKeys(reinterpret_cast<const char *>(payload), frame.len))
    return ESP_FAIL;
  payload[frame.len] = 0;
  StaticJsonDocument<4096> document;
  const auto error = deserializeJson(document, payload, frame.len);
  if (error || !document.is<JsonObject>())
    return ESP_FAIL;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
    return ESP_FAIL;
  const uint32_t stack_free_bytes = uxTaskGetStackHighWaterMark(nullptr);
  if (!http_stack_free_bytes_ || stack_free_bytes < http_stack_free_bytes_)
    http_stack_free_bytes_ = stack_free_bytes;
  if (client_fd_ >= 0 && client_fd_ != fd)
  {
    ++rejected_;
    xSemaphoreGive(mutex_);
    return ESP_FAIL;
  }
  if (client_fd_ < 0)
  {
    client_fd_ = fd;
    phase_ = Phase::Empty;
  }
  JsonObjectConst object = document.as<JsonObjectConst>();
  if (phase_ == Phase::Empty && same(object["kind"], "hello"))
    onHello(req, object);
  else if (phase_ == Phase::Challenge && same(object["kind"], "auth-response"))
    onResponse(req, object);
  else if (phase_ == Phase::Ready && same(object["kind"], "state"))
    onState(req, object);
  else
    reject(req);
  xSemaphoreGive(mutex_);
  return ESP_OK;
}
void LocalWssServer::onHello(httpd_req_t *req, JsonObjectConst f)
{
  time_t sent;
  const time_t now = time(nullptr);
  if (f.size() != 11 || f["schema"] != 1 || !same(f["link"], "local") ||
      !same(f["deviceRole"], "esp32") || !same(f["deviceId"], LOCAL_WSS_DEVICE_ID) ||
      !same(f["installationId"], LOCAL_WSS_INSTALLATION_ID) ||
      !textField(f["bootId"], 128) || !textField(f["sentAt"], 31) ||
      !uuid(f["deviceSessionId"]) || !uuid(f["connectionSessionId"]) ||
      !f["supportedVersions"].is<JsonArrayConst>() ||
      f["supportedVersions"].as<JsonArrayConst>().size() != 1 ||
      f["supportedVersions"][0] != 1 || !utc(f["sentAt"], &sent) ||
      llabs(static_cast<long long>(now - sent)) > 30 ||
      same(f["deviceSessionId"], last_device_session_id_) ||
      same(f["connectionSessionId"], last_connection_session_id_))
  {
    ++auth_failures_;
    reject(req);
    return;
  }
  strlcpy(boot_id_, f["bootId"], sizeof(boot_id_));
  strlcpy(device_session_id_, f["deviceSessionId"], sizeof(device_session_id_));
  strlcpy(connection_session_id_, f["connectionSessionId"], sizeof(connection_session_id_));
  randomHex(server_nonce_, 32);
  wireTime(now + 10, expires_at_, sizeof(expires_at_));
  StaticJsonDocument<512> answer;
  answer["schema"] = 1;
  answer["kind"] = "auth-challenge";
  answer["link"] = "local";
  answer["deviceRole"] = "esp32";
  answer["installationId"] = LOCAL_WSS_INSTALLATION_ID;
  answer["deviceId"] = LOCAL_WSS_DEVICE_ID;
  answer["bootId"] = boot_id_;
  answer["deviceSessionId"] = device_session_id_;
  answer["connectionSessionId"] = connection_session_id_;
  answer["serverNonce"] = server_nonce_;
  answer["expiresAt"] = expires_at_;
  char output[640];
  serializeJson(answer, output, sizeof(output));
  if (!sendJson(req, output))
  {
    reject(req);
    return;
  }
  phase_ = Phase::Challenge;
  phase_ms_ = millis();
  last_frame_ms_ = phase_ms_;
}
void LocalWssServer::onResponse(httpd_req_t *req, JsonObjectConst f)
{
  time_t expiry;
  const bool shape = f.size() == 13 && f["schema"] == 1 &&
                     same(f["link"], "local") && same(f["deviceRole"], "esp32") &&
                     same(f["installationId"], LOCAL_WSS_INSTALLATION_ID) &&
                     same(f["deviceId"], LOCAL_WSS_DEVICE_ID) &&
                     same(f["bootId"], boot_id_) && same(f["deviceSessionId"], device_session_id_) &&
                     same(f["connectionSessionId"], connection_session_id_) &&
                     same(f["serverNonce"], server_nonce_) && same(f["expiresAt"], expires_at_) &&
                     textField(f["clientNonce"], 64) && textField(f["mac"], 64) &&
                     utc(expires_at_, &expiry) && time(nullptr) < expiry &&
                     millis() - phase_ms_ < kAuthTimeoutMs;
  const bool valid = shape &&
                     (verifyMac(LOCAL_WSS_SECRET_HEX, f) ||
                      (LOCAL_WSS_SECOND_SECRET_HEX[0] && verifyMac(LOCAL_WSS_SECOND_SECRET_HEX, f)));
  // A challenge is single use even when a response is invalid.
  if (!valid)
  {
    ++auth_failures_;
    reject(req);
    return;
  }
  strlcpy(last_device_session_id_, device_session_id_, sizeof(last_device_session_id_));
  strlcpy(last_connection_session_id_, connection_session_id_, sizeof(last_connection_session_id_));
  StaticJsonDocument<512> answer;
  answer["schema"] = 1;
  answer["kind"] = "ready";
  answer["link"] = "local";
  answer["deviceRole"] = "esp32";
  answer["installationId"] = LOCAL_WSS_INSTALLATION_ID;
  answer["deviceId"] = LOCAL_WSS_DEVICE_ID;
  answer["bootId"] = boot_id_;
  answer["deviceSessionId"] = device_session_id_;
  answer["connectionSessionId"] = connection_session_id_;
  answer["selectedVersion"] = 1;
  char now_text[32];
  wireTime(time(nullptr), now_text, sizeof(now_text));
  answer["sentAt"] = now_text;
  char output[600];
  serializeJson(answer, output, sizeof(output));
  if (!sendJson(req, output))
  {
    reject(req);
    return;
  }
  phase_ = Phase::Ready;
  phase_ms_ = millis();
  last_frame_ms_ = phase_ms_;
  last_ping_ms_ = phase_ms_;
  awaiting_pong_ = false;
  telemetry_.connected = true;
  sockaddr_storage peer = {};
  socklen_t peer_size = sizeof(peer);
  if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr *>(&peer), &peer_size) == 0 &&
      peer.ss_family == AF_INET)
  {
    const auto *address = reinterpret_cast<const sockaddr_in *>(&peer);
    inet_ntop(AF_INET, &address->sin_addr, telemetry_.peer_ip, sizeof(telemetry_.peer_ip));
  }
  Serial.printf("Local WSS: authenticated peer=%s\n", telemetry_.peer_ip);
}
void LocalWssServer::onState(httpd_req_t *req, JsonObjectConst f)
{
  JsonObjectConst s = f["state"].as<JsonObjectConst>();
  JsonObjectConst valve = s["valve"].as<JsonObjectConst>();
  time_t observed;
  const time_t now = time(nullptr);
  const bool temperatures = (s["boilerTemperatureC"].isNull() ||
                             (s["boilerTemperatureC"].is<float>() && isfinite(s["boilerTemperatureC"].as<float>()) &&
                              s["boilerTemperatureC"].as<float>() >= -50 && s["boilerTemperatureC"].as<float>() <= 150)) &&
                            (s["solarTemperatureC"].isNull() ||
                             (s["solarTemperatureC"].is<float>() && isfinite(s["solarTemperatureC"].as<float>()) &&
                              s["solarTemperatureC"].as<float>() >= -50 && s["solarTemperatureC"].as<float>() <= 150));
  const bool valve_valid = valve.size() == 2 &&
                           (same(valve["reported"], "open") || same(valve["reported"], "closed") ||
                            same(valve["reported"], "moving") || same(valve["reported"], "unknown")) &&
                           (same(valve["reported"], "unknown") ? same(valve["evidence"], "unknown") : same(valve["evidence"], "inferred"));
  if (f.size() != 7 || f["schema"] != 1 || !same(f["link"], "local") ||
      !same(f["producerRole"], "esp32") ||
      !same(f["connectionSessionId"], connection_session_id_) ||
      !f["sequence"].is<uint32_t>() || s.size() != 11 ||
      s["schema"] != 1 || !same(s["source"], "original-esp32") ||
      !same(s["bootId"], boot_id_) || !same(s["deviceSessionId"], device_session_id_) ||
      !textField(s["observedAt"], 31) || !utc(s["observedAt"], &observed) ||
      observed > now + 30 || now - observed > 30 ||
      !(same(s["quality"], "reported") || same(s["quality"], "stale") ||
        same(s["quality"], "unknown")) ||
      !s["sequence"].is<uint32_t>() || f["sequence"] != s["sequence"] ||
      !s["manualMode"].is<bool>() || !temperatures || !valve_valid ||
      (telemetry_.valid && f["sequence"].as<uint32_t>() <= telemetry_.sequence &&
       same(s["deviceSessionId"], telemetry_.device_session_id)))
  {
    ++rejected_telemetry_;
    reject(req);
    return;
  }
  telemetry_.valid = true;
  telemetry_.connected = true;
  telemetry_.stale = !same(s["quality"], "reported");
  telemetry_.boiler_c = s["boilerTemperatureC"].isNull() ? NAN : s["boilerTemperatureC"].as<float>();
  telemetry_.solar_c = s["solarTemperatureC"].isNull() ? NAN : s["solarTemperatureC"].as<float>();
  telemetry_.manual = s["manualMode"].as<bool>();
  strlcpy(telemetry_.quality, s["quality"], sizeof(telemetry_.quality));
  strlcpy(telemetry_.valve, valve["reported"], sizeof(telemetry_.valve));
  strlcpy(telemetry_.boot_id, boot_id_, sizeof(telemetry_.boot_id));
  strlcpy(telemetry_.device_session_id, device_session_id_, sizeof(telemetry_.device_session_id));
  strlcpy(telemetry_.observed_at, s["observedAt"], sizeof(telemetry_.observed_at));
  telemetry_.sequence = f["sequence"].as<uint32_t>();
  telemetry_.received_ms = millis();
  telemetry_.observed_epoch = static_cast<int64_t>(observed);
  last_frame_ms_ = millis();
  ++accepted_;
}
#endif
