#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mbedtls/md.h>

namespace http_telemetry {
inline bool secret_key(const char* hex, uint8_t key[32]) {
    if (!hex || strlen(hex) != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = nibble(hex[i * 2]);
        const int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        key[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}
inline bool mac(const uint8_t key[32], const void* bytes, size_t length,
                uint8_t output[32]) {
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, 32,
                                   static_cast<const uint8_t*>(bytes), length,
                                   output) == 0;
}
inline bool mac_equal(const uint8_t* a, const uint8_t* b, size_t length) {
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) difference |= a[i] ^ b[i];
    return difference == 0;
}
inline bool verify_mac(const uint8_t key[32], const void* bytes, size_t length,
                       const uint8_t supplied[32]) {
    uint8_t expected[32]{};
    return mac(key, bytes, length, expected) &&
           mac_equal(expected, supplied, sizeof(expected));
}
inline void hex_encode(const uint8_t* bytes, size_t length, char* output) {
    static constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        output[i * 2] = digits[bytes[i] >> 4];
        output[i * 2 + 1] = digits[bytes[i] & 15];
    }
    output[length * 2] = 0;
}
inline bool hex_decode(const char* text, size_t length, uint8_t* output) {
    if (!text || strlen(text) != length * 2) return false;
    for (size_t i = 0; i < length; ++i) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        const int hi = nibble(text[i * 2]);
        const int lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        output[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}
} // namespace http_telemetry
