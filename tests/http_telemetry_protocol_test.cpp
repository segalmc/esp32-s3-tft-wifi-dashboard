#include "../include/HttpTelemetryProtocol.h"
#include <cassert>
#include <cstring>
using namespace http_telemetry;
int main() {
    uint8_t key[32]{};
    assert(secret_key("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key));
    uint8_t body[kMaxRequest]{};
    BatchHeader request{};
    request.magic=kBatchMagic; request.version=kVersion; request.kind=kBatchKindHeartbeat;
    request.device_id=9; request.sender_boot_nonce=10; request.challenge_nonce=11;
    memcpy(body,&request,sizeof(request));
    const size_t size=request_size(0);
    assert(mac(key,body,size-32,body+size-32));
    BatchHeader decoded{};
    assert(decode_request_header(body,size,9,decoded)==RequestStatus::ok);
    assert(verify_request_mac(key,body,size)==RequestStatus::ok);
    ChallengeGuard challenge;
    challenge.issue(11,100);
    assert(authenticate_request(key,challenge,101,body,size,9,decoded)==RequestStatus::ok);
    assert(authenticate_request(key,challenge,102,body,size,9,decoded)==RequestStatus::challenge);
    challenge.issue(11,200);
    body[sizeof(BatchHeader)-1]^=1;
    assert(authenticate_request(key,challenge,201,body,size,9,decoded)==RequestStatus::authentication);
    assert(authenticate_request(key,challenge,202,body,size,9,decoded)==RequestStatus::authentication);
    body[sizeof(BatchHeader)-1]^=1;
    assert(authenticate_request(key,challenge,203,body,size,9,decoded)==RequestStatus::ok);
    assert(decode_request_header(body,size-1,9,decoded)==RequestStatus::size);
    assert(decode_request_header(nullptr,size,9,decoded)==RequestStatus::size);
    request.count=21; request.kind=kBatchKindRecords;
    memcpy(body,&request,sizeof(request));
    assert(decode_request_header(body,size,9,decoded)==RequestStatus::header);
    Discovery discovery{};
    discovery.magic=kDiscoveryMagic; discovery.version=kVersion;
    discovery.kind=kDiscoveryReply; discovery.port=kHttpPort;
    discovery.device_id=9; discovery.boot_nonce=12; discovery.request_nonce=13;
    assert(mac(key,&discovery,offsetof(Discovery,mac),discovery.mac));
    assert(discovery_reply_valid(discovery,9,13));
    assert(verify_mac(key,&discovery,offsetof(Discovery,mac),discovery.mac));
    discovery.mac[0]^=1;
    assert(!verify_mac(key,&discovery,offsetof(Discovery,mac),discovery.mac));
    assert(!discovery_reply_valid(discovery,9,14));
}
