#pragma once
#include "HttpTelemetryAuth.h"
#include "HttpTelemetryTransport.h"
#include "HttpTelemetryWire.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace http_telemetry {
enum class RequestStatus : uint8_t { ok, size, header, challenge, authentication };
inline RequestStatus decode_request_header(const uint8_t* body, size_t length,
                                           uint64_t expected_device,
                                           BatchHeader& header) {
    if (!body || length < sizeof(BatchHeader) + 32 || length > kMaxRequest)
        return RequestStatus::size;
    memcpy(&header, body, sizeof(header));
    return valid_header(header, expected_device, length) ? RequestStatus::ok
                                                         : RequestStatus::header;
}
inline RequestStatus verify_request_mac(const uint8_t key[32],
                                        const uint8_t* body, size_t length) {
    if (!body || length < 32 || length > kMaxRequest) return RequestStatus::size;
    return verify_mac(key, body, length - 32, body + length - 32)
               ? RequestStatus::ok : RequestStatus::authentication;
}
inline RequestStatus authenticate_request(const uint8_t key[32],
                                          ChallengeGuard& challenge,
                                          uint32_t now_ms,
                                          const uint8_t* body, size_t length,
                                          uint64_t expected_device,
                                          BatchHeader& header) {
    const RequestStatus framing = decode_request_header(
        body, length, expected_device, header);
    if (framing != RequestStatus::ok) return framing;
    const RequestStatus authentication = verify_request_mac(key, body, length);
    if (authentication != RequestStatus::ok) return authentication;
    if (!challenge.consume(header.challenge_nonce, now_ms))
        return RequestStatus::challenge;
    return RequestStatus::ok;
}
} // namespace http_telemetry
