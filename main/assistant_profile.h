#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ASSISTANT_NAME_BYTES 49
#define ASSISTANT_DEFAULT_NAME "洛天依"
#define ASSISTANT_DEFAULT_THEME 0x66CCFFu

typedef enum {
    ASSISTANT_PERSONA_LUO_TIANYI = 0,
    ASSISTANT_PERSONA_COMPANION = 1,
    ASSISTANT_PERSONA_EXPERT = 2,
    ASSISTANT_PERSONA_COUNT,
} assistant_persona_t;

typedef struct {
    uint32_t theme_color;
    assistant_persona_t persona;
    char name[ASSISTANT_NAME_BYTES];
} assistant_profile_t;

void assistant_profile_defaults(assistant_profile_t *profile);
bool assistant_profile_valid(const assistant_profile_t *profile);
const char *assistant_persona_key(assistant_persona_t persona);
bool assistant_persona_from_key(const char *key, assistant_persona_t *persona);
