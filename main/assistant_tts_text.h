#pragma once

#include <stddef.h>

/* Conservative limit for mixed UTF-8 text (Tencent allows 150 Chinese chars).
 * Returns a complete UTF-8 prefix, preferring sentence/word boundaries.
 * Returns zero for empty or invalid UTF-8 input. No characters are discarded. */
#define ASSISTANT_TTS_SEGMENT_CHARS 120
#define ASSISTANT_TTS_SEGMENT_BYTES (ASSISTANT_TTS_SEGMENT_CHARS * 4)
size_t assistant_tts_segment_bytes(const char *text);
