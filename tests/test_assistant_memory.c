#include "assistant_memory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_utf8(void)
{
    char text[8] = "safe";
    assert(assistant_text_copy(text, 7, "你好世界"));
    assert(strcmp(text, "你好") == 0);
    assert(assistant_text_copy(text, 6, "你好"));
    assert(strcmp(text, "你") == 0);
    assert(assistant_text_copy(text, 4, "😀a"));
    assert(strcmp(text, "") == 0);
    assert(assistant_text_copy(text, 5, "😀a"));
    assert(strcmp(text, "😀") == 0);
    assert(!assistant_text_copy(text, sizeof(text), "\xc0\xaf"));
    assert(strcmp(text, "😀") == 0);
    assert(!assistant_text_valid("\xed\xa0\x80", 3));
    assert(!assistant_text_valid("\xf4\x90\x80\x80", 4));
    assert(!assistant_text_valid("\xe4\xbd", 2));
    assert(!assistant_text_valid("\x80", 1));
    assert(!assistant_text_valid("a\0b", 3));
    assert(assistant_text_valid("中文😀\n", strlen("中文😀\n")));
    assert(!assistant_text_copy(NULL, 3, "hi"));
    assert(!assistant_text_copy(text, 0, "hi"));
}

static void test_history(void)
{
    assistant_memory_t memory;
    assistant_memory_init(&memory);
    assert(assistant_memory_valid(&memory));
    assert(memory.count == 0);
    assert(assistant_memory_append(&memory, "one", "reply one"));
    assert(assistant_memory_append(&memory, "two", "reply two"));
    assert(assistant_memory_append(&memory, "three", "reply three"));
    assert(assistant_memory_append(&memory, "four", "reply four"));
    assert(memory.count == 3);
    assert(strcmp(memory.turns[0].user, "two") == 0);
    assert(strcmp(memory.turns[2].reply, "reply four") == 0);
    assistant_memory_t original = memory;
    assert(!assistant_memory_append(&memory, "", "ignored"));
    assert(!assistant_memory_append(&memory, "bad\xff", "ignored"));
    assert(memcmp(&memory, &original, sizeof(memory)) == 0);

    char large[4000];
    for (size_t i = 0; i < 3999; i += 3) memcpy(large + i, "好", 3);
    large[3999] = '\0';
    assert(assistant_memory_append(&memory, large, large));
    assert(strlen(memory.turns[2].user) == 510);
    assert(strlen(memory.turns[2].reply) == 1023);
    assert(assistant_memory_valid(&memory));
    memory.version = 2;
    assert(!assistant_memory_valid(&memory));
    memory = original;
    memory.count = ASSISTANT_MEMORY_TURNS + 1;
    assert(!assistant_memory_valid(&memory));
    memory = original;
    memset(memory.turns[0].user, 'x', sizeof(memory.turns[0].user));
    assert(!assistant_memory_valid(&memory));
    memory = original;
    memory.turns[0].reply[0] = '\xff';
    assert(!assistant_memory_valid(&memory));
    assistant_memory_init(&memory);
    assert(assistant_memory_valid(&memory));
    assert(memory.count == 0);
}

static uint32_t read32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void test_wav(void)
{
    uint8_t header[44];
    assert(assistant_wav_header(header, ASSISTANT_PCM_BYTES));
    assert(memcmp(header, "RIFF", 4) == 0);
    assert(memcmp(header + 8, "WAVEfmt ", 8) == 0);
    assert(memcmp(header + 36, "data", 4) == 0);
    assert(read32(header + 4) == ASSISTANT_PCM_BYTES + 36);
    assert(read32(header + 40) == ASSISTANT_PCM_BYTES);
    assert(read32(header + 24) == 16000);
    assert(read32(header + 28) == 32000);
    assert(header[20] == 1 && header[22] == 1 && header[32] == 2 && header[34] == 16);
    assert(!assistant_wav_header(header, 0));
    assert(!assistant_wav_header(header, 3));
    assert(!assistant_wav_header(header, ASSISTANT_PCM_BYTES + 2));
    assert(!assistant_wav_header(header, (size_t)-1));
    assert(!assistant_wav_header(NULL, 32));
}

int main(void)
{
    test_utf8();
    test_history();
    test_wav();
    puts("assistant memory and WAV tests passed");
    return 0;
}
