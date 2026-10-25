#include "assistant_tencent.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"


static void hex(const unsigned char digest[32], char out[65])
{
    static const char alphabet[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        out[i * 2] = alphabet[digest[i] >> 4];
        out[i * 2 + 1] = alphabet[digest[i] & 15];
    }
    out[64] = 0;
}

static bool hash(const char *text, char out[65])
{
    unsigned char digest[32];
    if (mbedtls_sha256((const unsigned char *)text, strlen(text), digest, 0)) return false;
    hex(digest, out);
    return true;
}

static bool hmac(const unsigned char *key, size_t bytes, const char *text, unsigned char out[32])
{
    return mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, bytes,
                           (const unsigned char *)text, strlen(text), out) == 0;
}

static bool tencent_sign(const char *service, const char *host,
                           const char *secret_id, const char *secret_key,
                           int64_t timestamp, char *out, size_t capacity)
{
    if (!service || !host || !secret_id || !secret_key || !out || timestamp < 1735689600) return false;
    time_t epoch = (time_t)timestamp;
    struct tm utc;
    char date[11], payload_hash[65], canonical_hash[65];
    if (!gmtime_r(&epoch, &utc) || strftime(date, sizeof(date), "%Y-%m-%d", &utc) != 10 ||
        !hash("UNSIGNED-PAYLOAD", payload_hash)) return false;
    char canonical[256];
    int n = snprintf(canonical, sizeof(canonical), "POST\n/\n\ncontent-type:" ASSISTANT_TENCENT_TYPE
                     "\nhost:%s\n\ncontent-type;host\n%s", host, payload_hash);
    if (n < 0 || (size_t)n >= sizeof(canonical) || !hash(canonical, canonical_hash)) return false;
    char to_sign[192], key[260];
    n = snprintf(to_sign, sizeof(to_sign), "TC3-HMAC-SHA256\n%lld\n%s/%s/tc3_request\n%s",
                 (long long)timestamp, date, service, canonical_hash);
    if (n < 0 || (size_t)n >= sizeof(to_sign)) return false;
    n = snprintf(key, sizeof(key), "TC3%s", secret_key);
    if (n < 0 || (size_t)n >= sizeof(key)) return false;
    unsigned char date_key[32], service_key[32], signing_key[32], signature[32];
    bool ok = hmac((unsigned char *)key, strlen(key), date, date_key) &&
              hmac(date_key, 32, service, service_key) &&
              hmac(service_key, 32, "tc3_request", signing_key) &&
              hmac(signing_key, 32, to_sign, signature);
    volatile unsigned char *wipe = (volatile unsigned char *)key;
    for (size_t i = 0; i < sizeof(key); ++i) wipe[i] = 0;
    wipe = date_key; for (size_t i = 0; i < 32; ++i) wipe[i] = 0;
    wipe = service_key; for (size_t i = 0; i < 32; ++i) wipe[i] = 0;
    wipe = signing_key; for (size_t i = 0; i < 32; ++i) wipe[i] = 0;
    if (!ok) return false;
    char signature_hex[65];
    hex(signature, signature_hex);
    n = snprintf(out, capacity, "TC3-HMAC-SHA256 Credential=%s/%s/%s/tc3_request, "
                 "SignedHeaders=content-type;host, Signature=%s", secret_id, date, service, signature_hex);
    return n > 0 && (size_t)n < capacity;
}

bool assistant_tencent_authorization(const char *secret_id, const char *secret_key,
                                     int64_t timestamp, char *out, size_t capacity)
{
    return tencent_sign("asr", "asr.tencentcloudapi.com", secret_id, secret_key, timestamp, out, capacity);
}

bool assistant_tencent_tts_authorization(const char *secret_id, const char *secret_key,
                                         int64_t timestamp, char *out, size_t capacity)
{
    return tencent_sign("tts", ASSISTANT_TENCENT_TTS_HOST, secret_id, secret_key, timestamp, out, capacity);
}

size_t assistant_base64(const uint8_t *input, size_t bytes, char *out, size_t capacity)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (bytes > (SIZE_MAX / 4 - 1) * 3) return 0;
    size_t needed = ((bytes + 2) / 3) * 4;
    if (capacity < needed + 1) return 0;
    size_t used = 0;
    for (size_t i = 0; i < bytes; i += 3) {
        uint32_t value = (uint32_t)input[i] << 16;
        if (i + 1 < bytes) value |= (uint32_t)input[i + 1] << 8;
        if (i + 2 < bytes) value |= input[i + 2];
        out[used++] = alphabet[value >> 18];
        out[used++] = alphabet[(value >> 12) & 63];
        out[used++] = i + 1 < bytes ? alphabet[(value >> 6) & 63] : '=';
        out[used++] = i + 2 < bytes ? alphabet[value & 63] : '=';
    }
    out[used] = 0;
    return used;
}
