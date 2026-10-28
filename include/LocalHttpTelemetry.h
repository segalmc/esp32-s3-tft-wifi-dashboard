#pragma once
#include "TftConfig.h"
#if TFT_HARDWARE_ENABLED
#include <Arduino.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <memory>
#include <freertos/semphr.h>
#include "NetworkManager.h"
#include "HttpTelemetryJournal.h"
#include "HttpTelemetryLittleFs.h"
#include "HttpTelemetryWire.h"
#include "HttpTelemetryTransport.h"

class LocalHttpTelemetry {
public:
    struct Snapshot {
        bool snapshot_ok = false;
        bool ever_message = false;
        bool ever_measurement = false;
        bool sensor_fault = false;
        float boiler_c = NAN;
        float solar_c = NAN;
        uint8_t valve = 0;
        uint8_t flags = 0;
        uint32_t communication_ms = 0;
        uint32_t measurement_ms = 0;
        uint64_t boot_nonce = 0;
        uint32_t sequence = 0;
        uint32_t epoch_s = 0;
        char peer_ip[16]{};
        uint32_t accepted = 0;
        uint32_t duplicates = 0;
        uint32_t auth_failures = 0;
        uint32_t storage_errors = 0;
    };
    void begin(NatureNetworkManager* network);
    void challenge(WebServer& web);
    void onRaw(WebServer& web);
    void receive(WebServer& web);
    Snapshot snapshot() const;
    bool ready() const { return ready_; }
private:
    static void discoveryEntry(void* context);
    void discoveryLoop();
    bool allowed(WebServer& web) const;
    void updateCommunication(const http_telemetry::BatchHeader& header,
                             const http_telemetry::Record* latest,
                             uint32_t now_ms, IPAddress peer);
    static uint64_t randomNonce();
    NatureNetworkManager* network_ = nullptr;
    http_telemetry::LittleFsJournalStore store_;
    std::unique_ptr<http_telemetry::Journal> journal_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
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
};
#endif
