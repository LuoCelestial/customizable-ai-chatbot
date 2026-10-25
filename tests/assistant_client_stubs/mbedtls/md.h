#pragma once
#include <stddef.h>
#define MBEDTLS_MD_SHA256 6
#ifdef __APPLE__
#include <CommonCrypto/CommonHMAC.h>
static inline const void *mbedtls_md_info_from_type(int type) { (void)type; return (void *)1; }
static inline int mbedtls_md_hmac(const void *info, const unsigned char *key, size_t size,
                                  const unsigned char *data, size_t bytes, unsigned char *out)
{ (void)info; CCHmac(kCCHmacAlgSHA256, key, size, data, bytes, out); return 0; }
#else
#include <openssl/hmac.h>
static inline const EVP_MD *mbedtls_md_info_from_type(int type) { (void)type; return EVP_sha256(); }
static inline int mbedtls_md_hmac(const EVP_MD *info, const unsigned char *key, size_t size,
                                  const unsigned char *data, size_t bytes, unsigned char *out)
{ return HMAC(info, key, (int)size, data, bytes, out, NULL) ? 0 : -1; }
#endif
