#include "LocalHttpTelemetry.h"
#if TFT_HARDWARE_ENABLED
#include "HttpTelemetryAuth.h"
#include "HttpTelemetryAcceptance.h"
#include "HttpTelemetryProtocol.h"
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <atomic>
#if __has_include("LocalHttpTelemetryConfig.h")
#include "LocalHttpTelemetryConfig.h"
#endif
#ifndef LOCAL_HTTP_DEVICE_ID
#define LOCAL_HTTP_DEVICE_ID ""
#define LOCAL_HTTP_SECRET_HEX ""
#endif

using namespace http_telemetry;
using namespace now_telemetry;

namespace {
std::atomic<uint32_t> now_tx_ok{0};
std::atomic<uint32_t> now_tx_fail{0};
std::atomic<uint32_t> now_rx_discovery{0};
std::atomic<uint32_t> now_rx_data{0};
std::atomic<uint32_t> now_rx_other{0};
std::atomic<uint32_t> now_rx_queue_full{0};

void now_diag_send_callback(
    const wifi_tx_info_t*,
    esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS)
        now_tx_ok.fetch_add(1, std::memory_order_relaxed);
    else
        now_tx_fail.fetch_add(1, std::memory_order_relaxed);

}

// Only nowLoop() calls this helper. It serializes ESP-NOW sends.
// The Wi-Fi callback only updates atomic counters.
bool now_diag_send(const uint8_t* peer, const uint8_t* data,
                   size_t length, const char* label) {
    const uint32_t before_ok =
        now_tx_ok.load(std::memory_order_acquire);
    const uint32_t before_fail =
        now_tx_fail.load(std::memory_order_acquire);

    const esp_err_t result = esp_now_send(peer, data, length);

    if (result != ESP_OK) {
        Serial.printf("[%s] submit_error=%d\n",
                      label, static_cast<int>(result));
        return false;
    }

    const uint32_t started = millis();

    while (millis() - started < 500) {
        const uint32_t ok =
            now_tx_ok.load(std::memory_order_acquire);
        const uint32_t fail =
            now_tx_fail.load(std::memory_order_acquire);

        if (ok != before_ok || fail != before_fail) {
            Serial.printf("[%s] result=%s elapsed_ms=%lu\n",
                          label,
                          fail != before_fail ? "FAIL" : "SUCCESS",
                          static_cast<unsigned long>(millis() - started));
            return fail == before_fail;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    Serial.printf("[%s] result=TIMEOUT\n", label);
    return false;
}
}

// V10: incoming telemetry is best-effort, bounded, volatile RAM.
// This buffer is NOT durable and does NOT assert delivery to NestJS.
// ESP-NOW and HTTP tasks must hold storage_mutex_ around accept() and stats().
// No stack-sized record arrays are allocated by this class.
class RecentTelemetryV10 {
    struct Entry {
        http_telemetry::Record record;
        uint32_t received_at_ms;
    };
    static constexpr uint32_t kMaximumAgeMs = 30UL * 60UL * 1000UL;
    static constexpr size_t kTargetCapacity = 4096;
    Entry* entries_ = nullptr;
    size_t capacity_ = 0;
    size_t start_ = 0;
    size_t count_ = 0;
    uint32_t evicted_ = 0;

    void expire(uint32_t now_ms) {
        while (count_ && uint32_t(now_ms - entries_[start_].received_at_ms) > kMaximumAgeMs) {
            start_ = (start_ + 1) % capacity_;
            --count_;
            ++evicted_;
        }
    }

    void insert(const http_telemetry::Record& record, uint32_t now_ms) {
        if (count_ == capacity_) {
            start_ = (start_ + 1) % capacity_;
            --count_;
            ++evicted_;
        }
        Entry& entry = entries_[(start_ + count_) % capacity_];
        entry.record = record;
        entry.received_at_ms = now_ms;
        ++count_;
    }

public:
    bool begin() {
        if (entries_) return true;
        // Try external RAM first. A small internal-RAM fallback remains bounded.
        constexpr size_t sizes[] = {kTargetCapacity, 1024, 256};
        for (size_t n : sizes) {
            entries_ = static_cast<Entry*>(heap_caps_malloc(
                n * sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (entries_) { capacity_ = n; break; }
        }
        if (!entries_) {
            constexpr size_t kSmallFallback = 128;
            entries_ = static_cast<Entry*>(heap_caps_malloc(
                kSmallFallback * sizeof(Entry), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (entries_) capacity_ = kSmallFallback;
        }
        return entries_ != nullptr;
    }

    http_telemetry::Acceptance accept(const http_telemetry::BatchHeader& header,
                                      const http_telemetry::Record* records,
                                      uint64_t device_id) {
        http_telemetry::Acceptance result{};
        if (!entries_ || !records || header.kind != http_telemetry::kBatchKindRecords ||
            header.count == 0 || header.count > http_telemetry::kMaxBatch) return result;
        const uint32_t now_ms = millis();
        expire(now_ms);
        for (uint8_t i = 0; i < header.count; ++i) {
            const http_telemetry::Record& record = records[i];
            if (!http_telemetry::valid(record, device_id)) break;
            // Best-effort retention: acknowledge but intentionally discard records
            // from an older boot or older than 30 minutes at the sender.
            // A caller may therefore retire source records even if not forwarded
            // to NestJS; this is deliberate under the user's loss-tolerant policy.
            const bool same_boot = record.boot_nonce == header.sender_boot_nonce;
            const bool recent_sample = same_boot &&
                record.uptime_ms <= header.sender_uptime_ms &&
                (header.sender_uptime_ms - record.uptime_ms) <= kMaximumAgeMs;
            if (!recent_sample) {
                ++result.count;
                continue;
            }
            bool duplicate = false;
            bool conflict = false;
            // Fast RAM-only scan, bounded by 4,096 entries; no filesystem I/O.
            for (size_t j = 0; j < count_; ++j) {
                const http_telemetry::Record& existing =
                    entries_[(start_ + j) % capacity_].record;
                if (!http_telemetry::same_id(existing, record)) continue;
                duplicate = true;
                if (memcmp(&existing, &record, sizeof(record)) != 0) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) { result.identity_conflict = true; break; }
            if (duplicate) ++result.duplicates;
            else { insert(record, now_ms); ++result.new_records; }
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

    size_t pending(uint32_t now_ms) {
        expire(now_ms);
        return count_;
    }
    size_t bytes() const { return count_ * sizeof(Entry); }
    size_t capacity_bytes() const { return capacity_ * sizeof(Entry); }
    uint32_t evictions() const { return evicted_; }
};

RecentTelemetryV10 recent_telemetry_v10;

LocalHttpTelemetry* LocalHttpTelemetry::now_instance_ = nullptr;
uint64_t LocalHttpTelemetry::randomNonce() {
    uint64_t value = 0;
    while (!value) value = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    return value;
}
void LocalHttpTelemetry::begin(NatureNetworkManager* network) {
    network_ = network;
    mutex_ = xSemaphoreCreateMutex();
    storage_mutex_ = xSemaphoreCreateMutex();
    now_queue_ = xQueueCreate(8, sizeof(NowRx));
    device_id_ = device_id_from_uuid(LOCAL_HTTP_DEVICE_ID);
    if (!mutex_ || !storage_mutex_ || !now_queue_ || !device_id_ ||
        !secret_key(LOCAL_HTTP_SECRET_HEX, key_)) {
        Serial.println("Local HTTP telemetry disabled: identity unavailable");
        return;
    }
    // Volatile telemetry is independent of LittleFS. The historical journal
    // remains on flash, untouched, but is intentionally not opened in V10.
    if (!recent_telemetry_v10.begin()) {
        Serial.println("Local HTTP telemetry disabled: RAM buffer allocation failed");
        return;
    }
    server_boot_ = randomNonce();
    ready_ = true;
    xTaskCreate(discoveryEntry, "http-discovery", 4096, this, 1, nullptr);
    xTaskCreate(nowEntry, "now-telemetry", 6144, this, 1, nullptr);
    Serial.printf("[TELEMETRY-V10] ready volatile=1 capacity_bytes=%lu max_age_min=30; no disk persistence\n",
                  static_cast<unsigned long>(recent_telemetry_v10.capacity_bytes()));
}
bool LocalHttpTelemetry::allowed(WebServer& web) const {
    if (!ready_ || !network_) return false;
    const auto wifi = network_->snapshot();
    return wifi.state == NatureNetworkManager::State::Online && !wifi.ap_active &&
           WiFi.status() == WL_CONNECTED && web.client().localIP() == WiFi.localIP();
}
void LocalHttpTelemetry::challenge(WebServer& web) {
    if (!allowed(web)) { web.send(503, "text/plain", "unavailable"); return; }
    const uint64_t nonce = randomNonce();
    challenge_.issue(nonce, millis());
    uint8_t challenge[16];
    memcpy(challenge, &nonce, 8);
    memcpy(challenge + 8, &server_boot_, 8);
    char answer[33];
    hex_encode(challenge, sizeof(challenge), answer);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain", answer);
}
void LocalHttpTelemetry::onRaw(WebServer& web) {
    const HTTPRaw& input = web.raw();
    if (input.status == RAW_START) {
        raw_length_ = 0;
        raw_complete_ = false;
        raw_bad_ = web.clientContentLength() > sizeof(raw_) ||
                   web.clientContentLength() < sizeof(BatchHeader) + 32;
    } else if (input.status == RAW_WRITE) {
        if (raw_bad_ || raw_length_ + input.currentSize > sizeof(raw_)) {
            raw_bad_ = true; return;
        }
        memcpy(raw_ + raw_length_, input.buf, input.currentSize);
        raw_length_ += input.currentSize;
    } else if (input.status == RAW_END) raw_complete_ = true;
    else if (input.status == RAW_ABORTED) raw_bad_ = true;
}
void LocalHttpTelemetry::receive(WebServer& web) {
    if (!allowed(web)) { web.send(503, "text/plain", "unavailable"); return; }
    if (raw_bad_ || !raw_complete_ || raw_length_ > sizeof(raw_) ||
        raw_length_ < sizeof(BatchHeader) + 32) {
        web.send(413, "text/plain", "body size"); return;
    }
    BatchHeader header{};
    const uint32_t now_ms = millis();
    const RequestStatus request = authenticate_request(
        key_, challenge_, now_ms, raw_, raw_length_, device_id_, header);
    if (request == RequestStatus::size || request == RequestStatus::header) {
        web.send(422, "text/plain", "header"); return;
    }
    if (request == RequestStatus::challenge) {
        web.send(409, "text/plain", "challenge"); return;
    }
    if (request == RequestStatus::authentication) {
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
            ++state_.auth_failures; xSemaphoreGive(mutex_);
        }
        web.send(401, "text/plain", "authentication"); return;
    }
    uint8_t accepted = 0;
    Acceptance result{};
    if (header.kind == kBatchKindRecords) {
        Record records[kMaxBatch]{};
        for (uint8_t index = 0; index < header.count; ++index)
            memcpy(&records[index], raw_ + sizeof(header) + index * sizeof(Record),
                   sizeof(Record));
        if (xSemaphoreTake(storage_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
            web.send(503, "text/plain", "storage busy"); return;
        }
        result = recent_telemetry_v10.accept(header, records, device_id_);
        xSemaphoreGive(storage_mutex_);
        accepted = result.count;
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
            state_.accepted += result.new_records;
            state_.duplicates += result.duplicates;
            if (result.storage_error) ++state_.storage_errors;
            xSemaphoreGive(mutex_);
        }
        if (!accepted) { web.send(507, "text/plain", "not accepted"); return; }
    }
    Ack ack{};
    ack.magic = kAckMagic;
    ack.version = kVersion;
    ack.count = accepted;
    ack.challenge_nonce = header.challenge_nonce;
    if (accepted) {
        Record last{};
        memcpy(&last, raw_ + sizeof(header) + (accepted - 1) * sizeof(last), sizeof(last));
        ack.last_boot_nonce = last.boot_nonce;
        ack.last_sequence = last.sequence;
    } else {
        ack.last_boot_nonce = header.reference_boot_nonce;
        ack.last_sequence = header.reference_sequence;
    }
    if (!mac(key_, &ack, offsetof(Ack, mac), ack.mac)) {
        web.send(500, "text/plain", "auth error"); return;
    }
    updateCommunication(header, result.has_current ? &result.current : nullptr, now_ms,
                        web.client().remoteIP(), LinkTransport::Http,
                        LinkTransport::Http, accepted > 0);
    char response[sizeof(Ack) * 2 + 1];
    hex_encode(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), response);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain", response);
}
void LocalHttpTelemetry::updateCommunication(const BatchHeader& header,
                                              const Record* latest,
                                              uint32_t now_ms, IPAddress peer,
                                              LinkTransport transport,
                                              LinkTransport intent, bool accepted_records) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    const bool valid_heartbeat = header.kind == kBatchKindHeartbeat &&
        state_.ever_measurement && header.reference_boot_nonce == state_.boot_nonce &&
        header.reference_sequence == state_.sequence && header.health == state_.flags;
    // Accepted historical records confirm link activity, not fresh measurements.
    if (latest || valid_heartbeat || accepted_records) {
        state_.ever_message = true;
        state_.communication_ms = now_ms;
        state_.transport = transport;
        state_.sender_intent = intent;
        if (transport == LinkTransport::EspNow) {
            state_.ever_now = true;
            state_.now_communication_ms = now_ms;
        } else if (transport == LinkTransport::Http) {
            state_.ever_http = true;
            state_.http_communication_ms = now_ms;
        }
    }
    if (latest && (!state_.ever_measurement ||
                   state_.boot_nonce != latest->boot_nonce ||
                   latest->sequence > state_.sequence)) {
        state_.ever_measurement = true;
        state_.measurement_ms = now_ms;
        state_.sample_age_at_receive_ms = header.sender_uptime_ms - latest->uptime_ms;
        state_.boot_nonce = latest->boot_nonce;
        state_.sequence = latest->sequence;
        state_.epoch_s = latest->flags & kClockValid ? latest->epoch_s : 0;
        state_.flags = latest->flags;
        state_.faults = latest->faults;
        state_.record_version = latest->version;
        state_.sensor_fault = latest->faults ||
                              (latest->flags & (kBoilerValid | kSolarValid)) !=
                                  (kBoilerValid | kSolarValid);
        state_.valve = latest->valve;
        state_.boiler_c = latest->flags & kBoilerValid ? latest->boiler_centi / 100.0f : NAN;
        state_.solar_c = latest->flags & kSolarValid ? latest->solar_centi / 100.0f : NAN;
        snprintf(state_.peer_ip, sizeof(state_.peer_ip), "%u.%u.%u.%u",
                 peer[0], peer[1], peer[2], peer[3]);
    }
    xSemaphoreGive(mutex_);
}
void LocalHttpTelemetry::nowReceive(const esp_now_recv_info_t* info,
                                    const uint8_t* data, int length) {
    auto* self = now_instance_;
    if (!self || !self->now_queue_ || !info || !info->src_addr || !data ||
        length <= 0 || length > static_cast<int>(sizeof(NowRx::bytes))) return;
    if (length == static_cast<int>(sizeof(DiscoveryFrame)))
        now_rx_discovery.fetch_add(1, std::memory_order_relaxed);
    else if (length >= static_cast<int>(sizeof(DataPrefix) + 32))
        now_rx_data.fetch_add(1, std::memory_order_relaxed);
    else
        now_rx_other.fetch_add(1, std::memory_order_relaxed);
    NowRx item{};
    memcpy(item.mac, info->src_addr, 6);
    item.length = length;
    memcpy(item.bytes, data, length);
    if (xQueueSend(self->now_queue_, &item, 0) != pdTRUE)
        now_rx_queue_full.fetch_add(1, std::memory_order_relaxed);
}
bool LocalHttpTelemetry::nowPeer(const uint8_t mac_addr[6], bool encrypted) {
    const bool existed = esp_now_is_peer_exist(mac_addr);

    const uint32_t tx_before =
        now_tx_ok.load(std::memory_order_relaxed) +
        now_tx_fail.load(std::memory_order_relaxed);

    esp_err_t delete_result = ESP_OK;

    if (existed) {
        delete_result = esp_now_del_peer(mac_addr);
    }

    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, mac_addr, 6);
    peer.ifidx = WIFI_IF_STA;
    peer.channel = 0;
    peer.encrypt = encrypted;

    if (encrypted) {
        memcpy(peer.lmk, key_ + 16, 16);
    }

    const esp_err_t add_result = esp_now_add_peer(&peer);

    const uint32_t tx_after =
        now_tx_ok.load(std::memory_order_relaxed) +
        now_tx_fail.load(std::memory_order_relaxed);

    Serial.printf(
        "[NOW-PEER-LIFE] mac=%02X:%02X:%02X:%02X:%02X:%02X "
        "existed=%u encrypted=%u delete_err=%d add_err=%d "
        "tx_before=%lu tx_after=%lu tx_ok=%lu tx_fail=%lu\n",
        mac_addr[0], mac_addr[1], mac_addr[2],
        mac_addr[3], mac_addr[4], mac_addr[5],
        static_cast<unsigned>(existed),
        static_cast<unsigned>(encrypted),
        static_cast<int>(delete_result),
        static_cast<int>(add_result),
        static_cast<unsigned long>(tx_before),
        static_cast<unsigned long>(tx_after),
        static_cast<unsigned long>(
            now_tx_ok.load(std::memory_order_relaxed)),
        static_cast<unsigned long>(
            now_tx_fail.load(std::memory_order_relaxed))
    );

    return add_result == ESP_OK;
}
void LocalHttpTelemetry::nowEntry(void* context) {
    static_cast<LocalHttpTelemetry*>(context)->nowLoop();
}
void LocalHttpTelemetry::nowLoop() {
    while (!(WiFi.getMode() & WIFI_STA)) vTaskDelay(pdMS_TO_TICKS(100));
    if (esp_now_init() != ESP_OK || esp_now_set_pmk(key_) != ESP_OK ||
        esp_now_register_recv_cb(nowReceive) != ESP_OK) {
        Serial.println("ESP-NOW: receiver initialization failed"); return;
    }
    now_instance_ = this;
    const esp_err_t tx_cb_result =
        esp_now_register_send_cb(now_diag_send_callback);
    Serial.printf("[NOW-TX] registration=%d\\n",
                  static_cast<int>(tx_cb_result));
    Serial.println("ESP-NOW: receiver ready");
    NowRx rx{};
    uint32_t last_rx_report_ms = millis();
    for (;;) {
        if (xQueueReceive(now_queue_, &rx, pdMS_TO_TICKS(1000)) != pdTRUE) continue;
        if (millis() - last_rx_report_ms >= 30000) {
            last_rx_report_ms = millis();
            Serial.printf("[NOW-RX-S3] discovery=%lu data=%lu other=%lu queue_full=%lu\n",
                          (unsigned long)now_rx_discovery.load(),
                          (unsigned long)now_rx_data.load(),
                          (unsigned long)now_rx_other.load(),
                          (unsigned long)now_rx_queue_full.load());
        }
        if (rx.length == sizeof(DiscoveryFrame)) {
            DiscoveryFrame request{};
            memcpy(&request, rx.bytes, sizeof(request));
            if (!discovery_valid(request, false, device_id_) ||
                !verify_mac(key_, &request, offsetof(DiscoveryFrame, mac), request.mac)) {
                if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                    ++state_.now_auth_failures; xSemaphoreGive(mutex_);
                }
                continue;
            }
            DiscoveryFrame reply = request;
            reply.receiver_boot = server_boot_;
            if (!mac(key_, &reply, offsetof(DiscoveryFrame, mac), reply.mac) ||
                !nowPeer(rx.mac, false)) continue;
            const bool discovery_tx_ok =
                now_diag_send(rx.mac,
                              reinterpret_cast<const uint8_t*>(&reply),
                              sizeof(reply), "NOW-DISCOVERY-TX");
            if (!discovery_tx_ok) continue;
            vTaskDelay(pdMS_TO_TICKS(40));
            const bool peer_ready = nowPeer(rx.mac, true);
            if (!peer_ready) {
                Serial.println("[NOW-DIAG] S3 encrypted peer setup FAILED");
                continue;
            }
            Serial.println("[NOW-DIAG] S3 encrypted peer ready");
            now_authorized_boot_ = request.sender_boot;
            memcpy(now_authorized_mac_, rx.mac, 6);
            continue;
        }
        if (rx.length < sizeof(DataPrefix) + 32) continue;
        const uint32_t data_processing_start_ms = millis();
        DataPrefix prefix{};
        memcpy(&prefix, rx.bytes, sizeof(prefix));
        if (!data_valid(prefix, rx.length, device_id_, server_boot_) ||
            !verify_mac(key_, rx.bytes, rx.length - 32,
                        rx.bytes + rx.length - 32)) {
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                ++state_.now_auth_failures; xSemaphoreGive(mutex_);
            }
            continue;
        }
        const BatchHeader& header = prefix.batch;
        if (header.sender_boot_nonce != now_authorized_boot_ ||
            memcmp(rx.mac, now_authorized_mac_, 6) != 0) continue;
        if (!now_replay_.allow(now_authorized_boot_, header.sender_boot_nonce,
                               header.sender_uptime_ms))
            continue;
        Record records[kMaxRecords]{};
        if (header.count)
            memcpy(records, rx.bytes + sizeof(prefix), header.count * sizeof(Record));
        Acceptance result{};
        uint32_t acceptance_duration_ms = 0;
        if (header.count) {
            if (xSemaphoreTake(storage_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) continue;
            const uint32_t acceptance_start_ms = millis();
            result = recent_telemetry_v10.accept(header, records, device_id_);
            acceptance_duration_ms = millis() - acceptance_start_ms;
            xSemaphoreGive(storage_mutex_);
            if (!result.count) continue;
        } else {
            const Snapshot current = snapshot();
            if (!current.ever_measurement ||
                current.boot_nonce != header.reference_boot_nonce ||
                current.sequence != header.reference_sequence ||
                current.flags != header.health) continue;
        }
        AckFrame frame{};
        frame.magic = kMagic; frame.kind = kAckFrame;
        frame.version = kVersion; frame.receiver_boot = server_boot_;
        frame.ack.magic = kAckMagic; frame.ack.version = kVersion;
        frame.ack.count = result.count;
        frame.ack.challenge_nonce = header.challenge_nonce;
        if (result.count) {
            frame.ack.last_boot_nonce = records[result.count - 1].boot_nonce;
            frame.ack.last_sequence = records[result.count - 1].sequence;
        } else {
            frame.ack.last_boot_nonce = header.reference_boot_nonce;
            frame.ack.last_sequence = header.reference_sequence;
        }
        if (!mac(key_, &frame, offsetof(AckFrame, ack) + offsetof(Ack, mac),
                 frame.ack.mac)) continue;
        now_replay_.accepted(header.sender_boot_nonce, header.sender_uptime_ms);
        updateCommunication(header, result.has_current ? &result.current : nullptr,
                            millis(), IPAddress(), LinkTransport::EspNow,
                            prefix.reserved == kIntentEspNow ? LinkTransport::EspNow :
                            LinkTransport::Http, result.count > 0);
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
            state_.accepted += result.new_records;
            state_.duplicates += result.duplicates;
            if (result.storage_error) ++state_.storage_errors;
            ++state_.now_acks;
            xSemaphoreGive(mutex_);
        }
        esp_now_peer_info_t ack_peer{};
        const esp_err_t peer_result =
            esp_now_get_peer(rx.mac, &ack_peer);
        Serial.printf(
            "[NOW-ACK-PEER] err=%d encrypted=%u ifidx=%d channel=%u\\n",
            static_cast<int>(peer_result),
            peer_result == ESP_OK ? static_cast<unsigned>(ack_peer.encrypt) : 0U,
            peer_result == ESP_OK ? static_cast<int>(ack_peer.ifidx) : -1,
            peer_result == ESP_OK ? static_cast<unsigned>(ack_peer.channel) : 0U);
        // This is an authenticated DATA/duplicate that reached ACK generation.
        // A MAC SUCCESS below is not proof that the original's RX callback fired.
        static uint32_t ack_generation_log = 0;
        if (++ack_generation_log <= 4 || ack_generation_log % 20 == 0)
            Serial.printf("[NOW-ACK-BATCH] count=%u new=%lu duplicate=%lu sender_uptime=%lu\n",
                          (unsigned)result.count,
                          (unsigned long)result.new_records,
                          (unsigned long)result.duplicates,
                          (unsigned long)header.sender_uptime_ms);
        const uint32_t ack_tx_start_ms = millis();
        const bool ack_tx_ok =
            now_diag_send(rx.mac,
                          reinterpret_cast<const uint8_t*>(&frame),
                          sizeof(frame), "NOW-ACK-TX");
        const uint32_t ack_processing_ms = ack_tx_start_ms - data_processing_start_ms;
        const uint32_t ack_total_ms = millis() - data_processing_start_ms;
        // Time measured only after dequeue. It excludes earlier queue latency.
        static uint32_t ack_timing_reports = 0;
        ++ack_timing_reports;
        if (ack_timing_reports <= 12 || ack_timing_reports % 20 == 0 ||
            ack_processing_ms > 350) {
            Serial.printf("[NOW-ACK-TIMING-V7] nonce_low=%08lx count=%u new=%lu duplicates=%lu "
                          "accept_ms=%lu pre_tx_ms=%lu total_ms=%lu mac_ok=%u\n",
                          static_cast<unsigned long>(static_cast<uint32_t>(header.challenge_nonce)),
                          static_cast<unsigned>(result.count),
                          static_cast<unsigned long>(result.new_records),
                          static_cast<unsigned long>(result.duplicates),
                          static_cast<unsigned long>(acceptance_duration_ms),
                          static_cast<unsigned long>(ack_processing_ms),
                          static_cast<unsigned long>(ack_total_ms),
                          static_cast<unsigned>(ack_tx_ok));
        }
        const esp_err_t ack_result =
            ack_tx_ok ? ESP_OK : ESP_FAIL;
        static uint32_t ack_submit_ok = 0;
        static uint32_t ack_submit_fail = 0;
        if (ack_result == ESP_OK) {
            ++ack_submit_ok;
        } else {
            ++ack_submit_fail;
            Serial.printf("[NOW-DIAG] S3 ACK submit FAILED err=%d\\n",
                          static_cast<int>(ack_result));
        }
        static uint32_t last_tx_report = 0;
        const uint32_t tx_total =
            now_tx_ok.load(std::memory_order_relaxed) +
            now_tx_fail.load(std::memory_order_relaxed);
        if (tx_total > last_tx_report) {
            last_tx_report = tx_total;
            Serial.printf(
                "[NOW-TX] callback ok=%lu fail=%lu ack_submitted=%lu\\n",
                static_cast<unsigned long>(
                    now_tx_ok.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(
                    now_tx_fail.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(ack_submit_ok));
        }
        if ((ack_submit_ok + ack_submit_fail) % 20 == 0) {
            Serial.printf("[NOW-DIAG] S3 ACK submit ok=%lu fail=%lu\\n",
                          static_cast<unsigned long>(ack_submit_ok),
                          static_cast<unsigned long>(ack_submit_fail));
        }
    }
}
LocalHttpTelemetry::Snapshot LocalHttpTelemetry::snapshot() const {
    Snapshot copy{};
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return copy;
    copy = state_;
    copy.snapshot_ok = true;
    xSemaphoreGive(mutex_);
    return copy;
}
void LocalHttpTelemetry::diagnostics() {
    if (!ready_) return;
    const Snapshot state = snapshot();
    size_t pending = 0, used = 0, total = 0;
    if (xSemaphoreTake(storage_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        pending = recent_telemetry_v10.pending(millis());
        used = recent_telemetry_v10.bytes();
        total = recent_telemetry_v10.capacity_bytes();
        xSemaphoreGive(storage_mutex_);
    }
    Serial.printf("Controller link: transport=%u intent=%u age_ms=%lu measurement_age_ms=%lu now_acks=%lu accepted=%lu duplicates=%lu auth_fail=%lu storage_errors=%lu pending=%lu used=%lu/%lu\n",
        static_cast<unsigned>(state.transport),
        static_cast<unsigned>(state.sender_intent),
        static_cast<unsigned long>(state.ever_message ? millis()-state.communication_ms : 0),
        static_cast<unsigned long>(state.ever_measurement ? millis()-state.measurement_ms : 0),
        static_cast<unsigned long>(state.now_acks),
        static_cast<unsigned long>(state.accepted),
        static_cast<unsigned long>(state.duplicates),
        static_cast<unsigned long>(state.auth_failures + state.now_auth_failures),
        static_cast<unsigned long>(state.storage_errors),
        static_cast<unsigned long>(pending),
        static_cast<unsigned long>(used),
        static_cast<unsigned long>(total));
}
void LocalHttpTelemetry::discoveryEntry(void* context) {
    static_cast<LocalHttpTelemetry*>(context)->discoveryLoop();
}
void LocalHttpTelemetry::discoveryLoop() {
    WiFiUDP udp;
    bool listening = false;
    uint32_t last_reply_ms = 0;
    for (;;) {
        const auto wifi = network_->snapshot();
        const bool online = ready_ && wifi.state == NatureNetworkManager::State::Online &&
                            !wifi.ap_active && WiFi.status() == WL_CONNECTED;
        if (!online && listening) { udp.stop(); listening = false; }
        if (online && !listening) listening = udp.begin(kDiscoveryPort) == 1;
        if (listening) {
            const int length = udp.parsePacket();
            if (length == static_cast<int>(sizeof(Discovery)) &&
                millis() - last_reply_ms >= 100) {
                Discovery request{};
                if (udp.read(reinterpret_cast<uint8_t*>(&request), sizeof(request)) ==
                    static_cast<int>(sizeof(request))) {
                    if (discovery_request_valid(request, device_id_) &&
                        verify_mac(key_, &request, offsetof(Discovery, mac), request.mac)) {
                        Discovery reply{};
                        reply.magic = kDiscoveryMagic; reply.version = kVersion;
                        reply.kind = kDiscoveryReply; reply.port = kHttpPort;
                        reply.device_id = device_id_; reply.boot_nonce = server_boot_;
                        reply.request_nonce = request.request_nonce;
                        if (mac(key_, &reply, offsetof(Discovery, mac), reply.mac)) {
                            udp.beginPacket(udp.remoteIP(), udp.remotePort());
                            udp.write(reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
                            udp.endPacket(); last_reply_ms = millis();
                        }
                    }
                }
            } else if (length > 0) {
                uint8_t discard[128];
                while (udp.available()) udp.read(discard, min(udp.available(), 128));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
#endif
