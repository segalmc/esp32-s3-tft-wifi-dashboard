#include "../include/HttpTelemetryWire.h"
#include <cassert>
using namespace http_telemetry;
int main() {
    const uint64_t id = device_id_from_uuid("00000000-0000-4000-8000-000000000001");
    assert(id && !device_id_from_uuid("00000000-0000-4000-8000-00000000000g"));
    BatchHeader h{};
    h.magic = kBatchMagic; h.version = kVersion; h.kind = kBatchKindRecords;
    h.count = 2; h.device_id = id; h.sender_boot_nonce = 55; h.challenge_nonce = 66;
    assert(valid_header(h, id, request_size(2)));
    assert(!valid_header(h, id + 1, request_size(2)));
    assert(!valid_header(h, id, request_size(1)));
    h.count = 21;
    assert(!valid_header(h, id, request_size(21)));
    h.count = 2;
    Record sent[2]{};
    sent[0].boot_nonce = sent[1].boot_nonce = 55;
    sent[0].sequence = 1; sent[1].sequence = 2;
    Ack a{};
    a.magic = kAckMagic; a.version = kVersion; a.count = 2;
    a.challenge_nonce = 66; a.last_boot_nonce = 55; a.last_sequence = 2;
    assert(ack_matches(a, h, sent, 2));
    a.last_sequence = 1;
    assert(!ack_matches(a, h, sent, 2));
    a.last_sequence = 2; a.challenge_nonce = 77;
    assert(!ack_matches(a, h, sent, 2));
    h.kind = kBatchKindHeartbeat; h.count = 0;
    h.reference_boot_nonce = 55; h.reference_sequence = 2;
    a.count = 0; a.challenge_nonce = 66;
    assert(valid_header(h, id, request_size(0)));
    assert(ack_matches(a, h, nullptr, 0));
    assert(ack_hex_length_valid(sizeof(Ack) * 2));
    assert(!ack_hex_length_valid(sizeof(Ack) * 2 - 1)); // interrupted response
    Discovery request{};
    request.magic = kDiscoveryMagic; request.version = kVersion;
    request.kind = kDiscoveryRequest; request.device_id = id;
    request.boot_nonce = 5; request.request_nonce = 6;
    assert(discovery_request_valid(request, id));
    request.port = 6000;
    assert(!discovery_request_valid(request, id));
    Discovery reply{};
    reply.magic = kDiscoveryMagic; reply.version = kVersion;
    reply.kind = kDiscoveryReply; reply.port = kHttpPort;
    reply.device_id = id; reply.boot_nonce = 7; reply.request_nonce = 6;
    assert(discovery_reply_valid(reply, id, 6));
    assert(!discovery_reply_valid(reply, id, 8)); // replayed response nonce
    reply.device_id++;
    assert(!discovery_reply_valid(reply, id, 6));
}
