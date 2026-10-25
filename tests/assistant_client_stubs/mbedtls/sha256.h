#pragma once
/* Use a real host crypto implementation, never a fake digest, for TC3 tests. */
#include <stddef.h>
#ifdef __APPLE__
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include <CommonCrypto/CommonDigest.h>
static inline int mbedtls_sha256(const unsigned char *data, size_t size, unsigned char *out, int sha224)
{ (void)sha224; return CC_SHA256(data, (CC_LONG)size, out) ? 0 : -1; }
#else
#include <openssl/sha.h>
static inline int mbedtls_sha256(const unsigned char *data, size_t size, unsigned char *out, int sha224)
{ (void)sha224; return SHA256(data, size, out) ? 0 : -1; }
#endif
