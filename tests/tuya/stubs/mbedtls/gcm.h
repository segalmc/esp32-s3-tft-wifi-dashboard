#pragma once
#include <openssl/evp.h>
#include <stdint.h>
#include <string.h>
struct mbedtls_gcm_context { uint8_t key[16]; };
#define MBEDTLS_CIPHER_ID_AES 1
#define MBEDTLS_GCM_ENCRYPT 1
inline void mbedtls_gcm_init(mbedtls_gcm_context*) {}
inline void mbedtls_gcm_free(mbedtls_gcm_context*) {}
inline int mbedtls_gcm_setkey(mbedtls_gcm_context* ctx, int, const unsigned char* key, unsigned bits) { if (bits != 128) return -1; memcpy(ctx->key, key, 16); return 0; }
inline int mbedtls_gcm_crypt_and_tag(mbedtls_gcm_context* ctx, int, size_t len,
    const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len,
    const unsigned char* input, unsigned char* output, size_t tag_len, unsigned char* tag) {
  EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new(); int done = 0;
  int ok = EVP_EncryptInit_ex(cipher, EVP_aes_128_gcm(), nullptr, nullptr, nullptr);
  ok &= EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
  ok &= EVP_EncryptInit_ex(cipher, nullptr, nullptr, ctx->key, iv);
  if (aad_len) ok &= EVP_EncryptUpdate(cipher, nullptr, &done, aad, aad_len);
  if (len) ok &= EVP_EncryptUpdate(cipher, output, &done, input, len);
  ok &= EVP_EncryptFinal_ex(cipher, output + done, &done);
  ok &= EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_GET_TAG, tag_len, tag);
  EVP_CIPHER_CTX_free(cipher); return ok ? 0 : -1;
}
inline int mbedtls_gcm_auth_decrypt(mbedtls_gcm_context* ctx, size_t len,
    const unsigned char* iv, size_t iv_len, const unsigned char* aad, size_t aad_len,
    const unsigned char* tag, size_t tag_len, const unsigned char* input, unsigned char* output) {
  EVP_CIPHER_CTX* cipher = EVP_CIPHER_CTX_new(); int done = 0;
  int ok = EVP_DecryptInit_ex(cipher, EVP_aes_128_gcm(), nullptr, nullptr, nullptr);
  ok &= EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_IVLEN, iv_len, nullptr);
  ok &= EVP_DecryptInit_ex(cipher, nullptr, nullptr, ctx->key, iv);
  if (aad_len) ok &= EVP_DecryptUpdate(cipher, nullptr, &done, aad, aad_len);
  if (len) ok &= EVP_DecryptUpdate(cipher, output, &done, input, len);
  ok &= EVP_CIPHER_CTX_ctrl(cipher, EVP_CTRL_GCM_SET_TAG, tag_len, const_cast<unsigned char*>(tag));
  ok &= EVP_DecryptFinal_ex(cipher, output + done, &done);
  EVP_CIPHER_CTX_free(cipher); return ok ? 0 : -1;
}
