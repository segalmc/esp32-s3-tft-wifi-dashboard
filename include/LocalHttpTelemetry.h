#pragma once
#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <esp_now.h>
#include <memory>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "NetworkManager.h"
#include "HttpTelemetryJournal.h"
#include "HttpTelemetryLittleFs.h"
#include "HttpTelemetryWire.h"
#include "HttpTelemetryTransport.h"
#include "NowTelemetryProtocol.h"
#include "NowReplayGuard.h"

class LocalHttpTelemetry {
public:
    enum class LinkTransport : uint8_t { None, EspNow, Http };
    struct Snapshot {
        bool snapshot_ok = false;
        bool ever_message = false;
        bool ever_measurement = false;
        bool sensor_fault = false;
        float boiler_c = NAN;
        float solar_c = NAN;
        uint8_t valve = 0;
        uint8_t flags = 0;
        uint8_t faults = 0;
        uint32_t communication_ms = 0;
        uint32_t measurement_ms = 0;
        uint32_t sample_age_at_receive_ms = 0;
        uint64_t boot_nonce = 0;
        uint32_t sequence = 0;
        uint32_t epoch_s = 0;
        char peer_ip[16]{};
        uint32_t accepted = 0;
        uint32_t duplicates = 0;
        uint32_t auth_failures = 0;
        uint32_t storage_errors = 0;
        LinkTransport transport = LinkTransport::None;
        LinkTransport sender_intent = LinkTransport::None;
        uint32_t now_communication_ms = 0;
        uint32_t http_communication_ms = 0;
        bool ever_now = false;
        bool ever_http = false;
        uint32_t now_acks = 0;
        uint32_t now_auth_failures = 0;
    };
    void begin(NatureNetworkManager* network);
    void challenge(WebServer& web);
    void onRaw(WebServer& web);
    void receive(WebServer& web);
    Snapshot snapshot() const;
    void diagnostics();
    bool ready() const { return ready_; }
private:
    struct NowRx {
        uint8_t mac[6];
        uint16_t length;
        uint8_t bytes[sizeof(now_telemetry::DataPrefix) +
                      now_telemetry::kMaxRecords * sizeof(http_telemetry::Record) + 32];
    };
    static void nowReceive(const esp_now_recv_info_t* info,
                           const uint8_t* data, int length);
    static void nowEntry(void* context);
    void nowLoop();
    bool nowPeer(const uint8_t mac[6], bool encrypted);
    static void discoveryEntry(void* context);
    void discoveryLoop();
    bool allowed(WebServer& web) const;
    void updateCommunication(const http_telemetry::BatchHeader& header,
                             const http_telemetry::Record* latest,
                             uint32_t now_ms, IPAddress peer,
                             LinkTransport transport, LinkTransport intent);
    static uint64_t randomNonce();
    NatureNetworkManager* network_ = nullptr;
    http_telemetry::LittleFsJournalStore store_;
    std::unique_ptr<http_telemetry::Journal> journal_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    SemaphoreHandle_t storage_mutex_ = nullptr;
    QueueHandle_t now_queue_ = nullptr;
    Snapshot state_{};
    uint8_t key_[32]{};
    uint64_t device_id_ = 0;
    uint64_t server_boot_ = 0;
    http_telemetry::ChallengeGuard challenge_;
    uint8_t raw_[http_telemetry::kMaxRequest]{};
    size_t raw_length_ = 0;
    bool raw_complete_ = false;
    bool raw_bad_ = false;
    bool ready_ = false;
    uint64_t now_authorized_boot_ = 0;
    uint8_t now_authorized_mac_[6]{};
    now_telemetry::ReplayGuard now_replay_;
    static LocalHttpTelemetry* now_instance_;
};
#endif
