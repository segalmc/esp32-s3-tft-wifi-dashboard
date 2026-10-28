#include "../include/HttpTelemetryAuth.h"
#include <cassert>
#include <cstring>
using namespace http_telemetry;
int main() {
    uint8_t key[32]{}, first[32]{}, second[32]{};
    assert(secret_key("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key));
    assert(!secret_key("0001", key));
    const char body[] = "authenticated-http-body";
    assert(mac(key, body, sizeof(body) - 1, first));
    assert(mac(key, body, sizeof(body) - 1, second));
    assert(mac_equal(first, second, sizeof(first)));
    assert(verify_mac(key, body, sizeof(body) - 1, second));
    second[12] ^= 1;
    assert(!mac_equal(first, second, sizeof(first)));
    assert(!verify_mac(key, body, sizeof(body) - 1, second));
    char encoded[65]{};
    hex_encode(first, sizeof(first), encoded);
    uint8_t decoded[32]{};
    assert(hex_decode(encoded, sizeof(decoded), decoded));
    assert(memcmp(first, decoded, sizeof(first)) == 0);
    encoded[3] = 'G';
    assert(!hex_decode(encoded, sizeof(decoded), decoded));
}
