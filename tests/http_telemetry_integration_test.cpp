// Reuse the same fault-injectable store and record fixture as acceptance tests.
#define main acceptance_fixture_main
#include "http_telemetry_acceptance_test.cpp"
#undef main
#include "../include/HttpTelemetryProtocol.h"

int main() {
    MemoryStore source_store, receiver_store;
    Journal source(source_store, 3, 10), receiver(receiver_store, 3, 10);
    assert(source.begin() && receiver.begin());
    assert(source.append(record(1)) && source.append(record(2)));
    Record batch[20]{};
    assert(source.batch(batch, 20) == 2);
    BatchHeader header{};
    header.magic = kBatchMagic; header.version = kVersion;
    header.kind = kBatchKindRecords; header.count = 2; header.device_id = 3;
    header.sender_boot_nonce = 4; header.sender_uptime_ms = 1100;
    header.challenge_nonce = 77;
    assert(valid_header(header, 3, request_size(2)));
    uint8_t key[32]{};
    assert(secret_key("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key));
    uint8_t request[kMaxRequest]{};
    memcpy(request, &header, sizeof(header));
    memcpy(request + sizeof(header), batch, 2 * sizeof(Record));
    const size_t request_bytes = request_size(2);
    assert(mac(key, request, request_bytes - 32, request + request_bytes - 32));
    ChallengeGuard guard;
    guard.issue(77, 100);
    BatchHeader authenticated{};
    assert(authenticate_request(key, guard, 101, request, request_bytes, 3,
                                authenticated) == RequestStatus::ok);
    request[request_bytes - 1] ^= 1;
    guard.issue(77, 102);
    assert(authenticate_request(key, guard, 103, request, request_bytes, 3,
                                authenticated) == RequestStatus::authentication);
    assert(receiver.pending() == 0);
    request[request_bytes - 1] ^= 1;
    auto first = accept_batch(receiver, header, batch, 3);
    assert(first.count == 2 && first.new_records == 2 && source.pending() == 2);
    // The receiver rebooted after persistence and its response was lost.
    Journal recovered_receiver(receiver_store, 3, 10);
    assert(recovered_receiver.begin());
    auto retry = accept_batch(recovered_receiver, header, batch, 3);
    assert(retry.count == 2 && retry.duplicates == 2 && recovered_receiver.pending() == 2);
    Ack ack{};
    ack.magic = kAckMagic; ack.version = kVersion; ack.count = retry.count;
    ack.challenge_nonce = header.challenge_nonce;
    ack.last_boot_nonce = batch[1].boot_nonce; ack.last_sequence = batch[1].sequence;
    Ack bad = ack; ++bad.last_sequence;
    assert(!ack_matches(bad, header, batch, 2) && source.pending() == 2);
    assert(ack_matches(ack, header, batch, 2) && source.acknowledge(ack.count));
    Journal recovered_source(source_store, 3, 10);
    assert(recovered_source.begin() && recovered_source.pending() == 0);
}
