#include "../include/NowTelemetryProtocol.h"
#include <cassert>
using namespace now_telemetry;
int main() {
    DiscoveryFrame request{};
    request.magic = kMagic; request.kind = kDiscoveryFrame;
    request.version = kVersion; request.device_id = 1;
    request.sender_boot = 2; request.nonce = 3;
    assert(discovery_valid(request, false, 1));
    request.receiver_boot = 4;
    assert(discovery_valid(request, true, 1, 3));
    assert(!discovery_valid(request, true, 1, 5));
    DataPrefix data{};
    data.magic = kMagic; data.kind = kDataFrame;
    data.version = kVersion; data.receiver_boot = 4;
    data.reserved = kIntentEspNow;
    data.batch.magic = kBatchMagic; data.batch.version = kVersion;
    data.batch.kind = kBatchKindHeartbeat; data.batch.device_id = 1;
    data.batch.sender_boot_nonce = 2; data.batch.challenge_nonce = 5;
    assert(data_valid(data, data_size(0), 1, 4));
    data.reserved = kIntentHttp;
    assert(data_valid(data, data_size(0), 1, 4));
    data.reserved = 0;
    assert(!data_valid(data, data_size(0), 1, 4));
    data.reserved = kIntentEspNow;
    assert(!data_valid(data, data_size(0), 1, 9));
    assert(!data_valid(data, data_size(1), 1, 4));
}
