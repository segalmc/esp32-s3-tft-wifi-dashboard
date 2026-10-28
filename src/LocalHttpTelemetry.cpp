#include "LocalHttpTelemetry.h"
#if TFT_HARDWARE_ENABLED
#include "HttpTelemetryAuth.h"
#include "HttpTelemetryAcceptance.h"
#include "HttpTelemetryProtocol.h"
#include <esp_system.h>
#include <cstring>
#if __has_include("LocalHttpTelemetryConfig.h")
#include "LocalHttpTelemetryConfig.h"
#endif
#ifndef LOCAL_HTTP_DEVICE_ID
#define LOCAL_HTTP_DEVICE_ID ""
#define LOCAL_HTTP_SECRET_HEX ""
#endif

using namespace http_telemetry;
uint64_t LocalHttpTelemetry::randomNonce() {
    uint64_t value = 0;
    while (!value) value = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
    return value;
}
void LocalHttpTelemetry::begin(NatureNetworkManager* network) {
    network_ = network;
    mutex_ = xSemaphoreCreateMutex();
    device_id_ = device_id_from_uuid(LOCAL_HTTP_DEVICE_ID);
    if (!mutex_ || !device_id_ || !secret_key(LOCAL_HTTP_SECRET_HEX, key_)) {
        Serial.println("Local HTTP telemetry disabled: identity unavailable");
        return;
    }
    if (!store_.begin() || store_.total_bytes() < 1048576) {
        Serial.println("Local HTTP telemetry disabled: LittleFS not mounted or too small");
        return;
    }
    journal_.reset(new Journal(store_, device_id_, 60000));
    if (!journal_ || !journal_->begin()) {
        Serial.println("Local HTTP telemetry disabled: journal recovery failed");
        return;
    }
    server_boot_ = randomNonce();
    ready_ = true;
    xTaskCreate(discoveryEntry, "http-discovery", 4096, this, 1, nullptr);
    Serial.printf("Local HTTP telemetry ready: pending=%lu storage=%lu/%lu B\n",
                  static_cast<unsigned long>(journal_->pending()),
                  static_cast<unsigned long>(store_.used_bytes()),
                  static_cast<unsigned long>(store_.total_bytes()));
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
        result = accept_batch(*journal_, header, records, device_id_);
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
                        web.client().remoteIP());
    char response[sizeof(Ack) * 2 + 1];
    hex_encode(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack), response);
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "text/plain", response);
}
void LocalHttpTelemetry::updateCommunication(const BatchHeader& header,
                                              const Record* latest,
                                              uint32_t now_ms, IPAddress peer) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(20)) != pdTRUE) return;
    state_.ever_message = true;
    state_.communication_ms = now_ms;
    if (latest && (!state_.ever_measurement ||
                   state_.boot_nonce != latest->boot_nonce ||
                   latest->sequence > state_.sequence)) {
        state_.ever_measurement = true;
        state_.measurement_ms = now_ms;
        state_.boot_nonce = latest->boot_nonce;
        state_.sequence = latest->sequence;
        state_.epoch_s = latest->flags & kClockValid ? latest->epoch_s : 0;
        state_.flags = latest->flags;
        state_.sensor_fault = latest->faults ||
                              (latest->flags & (kBoilerValid | kSolarValid)) !=
                                  (kBoilerValid | kSolarValid);
        state_.valve = latest->valve;
        state_.boiler_c = latest->flags & kBoilerValid ? latest->boiler_centi / 100.0f : NAN;
        state_.solar_c = latest->flags & kSolarValid ? latest->solar_centi / 100.0f : NAN;
        snprintf(state_.peer_ip, sizeof(state_.peer_ip), "%u.%u.%u.%u",
                 peer[0], peer[1], peer[2], peer[3]);
    } else if (header.kind == kBatchKindHeartbeat && state_.ever_measurement &&
               header.reference_boot_nonce == state_.boot_nonce &&
               header.reference_sequence == state_.sequence &&
               header.health == state_.flags) {
        state_.measurement_ms = now_ms;
    }
    xSemaphoreGive(mutex_);
}
LocalHttpTelemetry::Snapshot LocalHttpTelemetry::snapshot() const {
    Snapshot copy{};
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(5)) != pdTRUE) return copy;
    copy = state_;
    copy.snapshot_ok = true;
    xSemaphoreGive(mutex_);
    return copy;
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
