#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ASSISTANT_TENCENT_URL "https://asr.tencentcloudapi.com/"
#define ASSISTANT_TENCENT_TYPE "application/json; charset=utf-8"

/* TC3's HTTPS unsigned-payload mode permits bounded streaming of live audio.
 * The payload marker is SHA-256 hashed, matching Tencent's official SDK. */
bool assistant_tencent_authorization(const char *secret_id, const char *secret_key,
                                     int64_t timestamp, char *out, size_t capacity);
#define ASSISTANT_TENCENT_TTS_URL "https://tts.tencentcloudapi.com/"
#define ASSISTANT_TENCENT_TTS_HOST "tts.tencentcloudapi.com"
bool assistant_tencent_tts_authorization(const char *secret_id, const char *secret_key,
                                         int64_t timestamp, char *out, size_t capacity);
/* Input chunks must be multiples of three except for the final chunk. */
size_t assistant_base64(const uint8_t *input, size_t bytes, char *out, size_t capacity);
static inline bool assistant_tencent_identifier(const char *text, size_t capacity)
{
    const char *end = memchr(text, 0, capacity);
    if (!end || end == text) return false;
    for (; text < end; ++text) {
        unsigned char c = (unsigned char)*text;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

