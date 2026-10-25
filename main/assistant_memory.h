#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ASSISTANT_MEMORY_TURNS 3
#define ASSISTANT_MEMORY_USER_BYTES 512
#define ASSISTANT_MEMORY_REPLY_BYTES 1024
#define ASSISTANT_SAMPLE_RATE 16000
#define ASSISTANT_RECORD_SECONDS 8
#define ASSISTANT_PCM_BYTES (ASSISTANT_SAMPLE_RATE * ASSISTANT_RECORD_SECONDS * 2)

typedef struct {
    char user[ASSISTANT_MEMORY_USER_BYTES + 1];
    char reply[ASSISTANT_MEMORY_REPLY_BYTES + 1];
} assistant_turn_t;

/* Versioned fixed-size NVS blob. No pointers, padding-dependent fields or secrets. */
typedef struct {
    uint32_t version;
    uint32_t count;
    assistant_turn_t turns[ASSISTANT_MEMORY_TURNS];
} assistant_memory_t;

void assistant_memory_init(assistant_memory_t *memory);
bool assistant_memory_valid(const assistant_memory_t *memory);
bool assistant_memory_append(assistant_memory_t *memory, const char *user, const char *reply);
bool assistant_text_valid(const char *text, size_t bytes);
/* Copies a valid UTF-8 prefix; returns false for invalid input, never splits a glyph. */
bool assistant_text_copy(char *dest, size_t capacity, const char *source);
/* PCM little-endian, 16-bit, 16 kHz mono. Rejects odd, zero or oversized lengths. */
bool assistant_wav_header(uint8_t header[44], size_t pcm_bytes);
