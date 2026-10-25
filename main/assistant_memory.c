#include "assistant_memory.h"

#include <string.h>

static size_t utf8_width(const unsigned char *s, size_t remaining)
{
    unsigned c = s[0];
    if (c < 0x80) return c ? 1 : 0;
    size_t width = c >= 0xc2 && c <= 0xdf ? 2 :
                   c >= 0xe0 && c <= 0xef ? 3 :
                   c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!width || width > remaining) return 0;
    for (size_t i = 1; i < width; ++i) {
        if ((s[i] & 0xc0) != 0x80) return 0;
    }
    if ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0) ||
        (c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90)) return 0;
    return width;
}

bool assistant_text_valid(const char *text, size_t bytes)
{
    if (!text) return false;
    for (size_t i = 0; i < bytes;) {
        size_t width = utf8_width((const unsigned char *)text + i, bytes - i);
        if (!width) return false;
        i += width;
    }
    return true;
}

bool assistant_text_copy(char *dest, size_t capacity, const char *source)
{
    if (!dest || !capacity || !source) return false;
    size_t bytes = strlen(source);
    if (!assistant_text_valid(source, bytes)) return false;
    size_t length = 0;
    while (length < bytes) {
        size_t width = utf8_width((const unsigned char *)source + length, bytes - length);
        if (length + width >= capacity) break;
        length += width;
    }
    memmove(dest, source, length);
    dest[length] = '\0';
    return true;
}

void assistant_memory_init(assistant_memory_t *memory)
{
    if (!memory) return;
    memset(memory, 0, sizeof(*memory));
    memory->version = 1;
}

static bool valid_stored_text(const char *text, size_t capacity)
{
    const char *end = memchr(text, '\0', capacity);
    return end && end != text && assistant_text_valid(text, (size_t)(end - text));
}

bool assistant_memory_valid(const assistant_memory_t *memory)
{
    if (!memory || memory->version != 1 || memory->count > ASSISTANT_MEMORY_TURNS) return false;
    for (unsigned i = 0; i < memory->count; ++i) {
        if (!valid_stored_text(memory->turns[i].user, sizeof(memory->turns[i].user)) ||
            !valid_stored_text(memory->turns[i].reply, sizeof(memory->turns[i].reply))) return false;
    }
    return true;
}

bool assistant_memory_append(assistant_memory_t *memory, const char *user, const char *reply)
{
    if (!assistant_memory_valid(memory) || !user || !reply || !user[0] || !reply[0] ||
        !assistant_text_valid(user, strlen(user)) || !assistant_text_valid(reply, strlen(reply))) return false;
    /* Validate before trimming, so failed input preserves the original history. */
    if (memory->count == ASSISTANT_MEMORY_TURNS) {
        memmove(&memory->turns[0], &memory->turns[1],
                sizeof(memory->turns[0]) * (ASSISTANT_MEMORY_TURNS - 1));
        --memory->count;
    }
    assistant_turn_t *turn = &memory->turns[memory->count++];
    memset(turn, 0, sizeof(*turn));
    assistant_text_copy(turn->user, sizeof(turn->user), user);
    assistant_text_copy(turn->reply, sizeof(turn->reply), reply);
    return true;
}

static void little32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

bool assistant_wav_header(uint8_t header[44], size_t pcm_bytes)
{
    if (!header || !pcm_bytes || (pcm_bytes & 1) || pcm_bytes > ASSISTANT_PCM_BYTES) return false;
    memset(header, 0, 44);
    memcpy(header, "RIFF", 4);
    little32(header + 4, (uint32_t)pcm_bytes + 36);
    memcpy(header + 8, "WAVEfmt ", 8);
    little32(header + 16, 16);
    header[20] = 1; /* PCM */
    header[22] = 1; /* mono */
    little32(header + 24, ASSISTANT_SAMPLE_RATE);
    little32(header + 28, ASSISTANT_SAMPLE_RATE * 2);
    header[32] = 2;
    header[34] = 16;
    memcpy(header + 36, "data", 4);
    little32(header + 40, (uint32_t)pcm_bytes);
    return true;
}
