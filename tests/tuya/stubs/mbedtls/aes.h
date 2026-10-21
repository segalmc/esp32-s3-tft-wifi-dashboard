#pragma once
#include <openssl/aes.h>
#include <stdint.h>
struct mbedtls_aes_context { AES_KEY key; };
#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0
inline void mbedtls_aes_init(mbedtls_aes_context*) {}
inline void mbedtls_aes_free(mbedtls_aes_context*) {}
inline int mbedtls_aes_setkey_enc(mbedtls_aes_context* ctx, const unsigned char* key, unsigned bits) { return AES_set_encrypt_key(key, bits, &ctx->key); }
inline int mbedtls_aes_setkey_dec(mbedtls_aes_context* ctx, const unsigned char* key, unsigned bits) { return AES_set_decrypt_key(key, bits, &ctx->key); }
inline int mbedtls_aes_crypt_ecb(mbedtls_aes_context* ctx, int mode, const unsigned char* input, unsigned char* output) { AES_ecb_encrypt(input, output, &ctx->key, mode == MBEDTLS_AES_ENCRYPT ? AES_ENCRYPT : AES_DECRYPT); return 0; }
