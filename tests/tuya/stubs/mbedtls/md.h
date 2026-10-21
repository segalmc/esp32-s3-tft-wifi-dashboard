#pragma once
#include <openssl/hmac.h>
#include <stdint.h>
#include <vector>
#define MBEDTLS_MD_SHA256 1
struct mbedtls_md_context_t { std::vector<uint8_t> key, data; };
inline void mbedtls_md_init(mbedtls_md_context_t*) {}
inline void mbedtls_md_free(mbedtls_md_context_t*) {}
inline const void* mbedtls_md_info_from_type(int) { return nullptr; }
inline int mbedtls_md_setup(mbedtls_md_context_t*, const void*, int) { return 0; }
inline int mbedtls_md_hmac_starts(mbedtls_md_context_t* ctx, const unsigned char* key, size_t len) { ctx->key.assign(key, key + len); ctx->data.clear(); return 0; }
inline int mbedtls_md_hmac_update(mbedtls_md_context_t* ctx, const unsigned char* data, size_t len) { ctx->data.insert(ctx->data.end(), data, data + len); return 0; }
inline int mbedtls_md_hmac_finish(mbedtls_md_context_t* ctx, unsigned char* output) { unsigned len = 0; HMAC(EVP_sha256(), ctx->key.data(), ctx->key.size(), ctx->data.data(), ctx->data.size(), output, &len); return len == 32 ? 0 : -1; }
