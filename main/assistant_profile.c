#include "assistant_profile.h"

#include <string.h>

#include "assistant_memory.h"

void assistant_profile_defaults(assistant_profile_t *profile)
{
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
    profile->theme_color = ASSISTANT_DEFAULT_THEME;
    profile->persona = ASSISTANT_PERSONA_LUO_TIANYI;
    strcpy(profile->name, ASSISTANT_DEFAULT_NAME);
}

bool assistant_profile_valid(const assistant_profile_t *profile)
{
    if (!profile || profile->theme_color > 0xFFFFFFu ||
        profile->persona < 0 || profile->persona >= ASSISTANT_PERSONA_COUNT) return false;
    const char *end = memchr(profile->name, '\0', sizeof(profile->name));
    if (!end || end == profile->name) return false;
    size_t bytes = (size_t)(end - profile->name);
    if (!assistant_text_valid(profile->name, bytes)) return false;
    unsigned glyphs = 0;
    bool has_non_space = false;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)profile->name[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (c > 0x20) has_non_space = true;
        if ((c & 0xc0) != 0x80) ++glyphs;
    }
    return has_non_space && glyphs <= 16;
}

const char *assistant_persona_key(assistant_persona_t persona)
{
    switch (persona) {
        case ASSISTANT_PERSONA_LUO_TIANYI: return "luotianyi";
        case ASSISTANT_PERSONA_COMPANION: return "companion";
        case ASSISTANT_PERSONA_EXPERT: return "expert";
        default: return NULL;
    }
}

bool assistant_persona_from_key(const char *key, assistant_persona_t *persona)
{
    if (!key || !persona) return false;
    if (!strcmp(key, "luotianyi")) *persona = ASSISTANT_PERSONA_LUO_TIANYI;
    else if (!strcmp(key, "companion")) *persona = ASSISTANT_PERSONA_COMPANION;
    else if (!strcmp(key, "expert")) *persona = ASSISTANT_PERSONA_EXPERT;
    else return false;
    return true;
}
