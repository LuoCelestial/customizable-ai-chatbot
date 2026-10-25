#include "assistant_tts_text.h"
#include <stdint.h>

size_t assistant_tts_segment_bytes(const char *text)
{
    if (!text) return 0;
    size_t offset = 0, sentence = 0, word = 0;
    unsigned count = 0;
    while (text[offset] && count < ASSISTANT_TTS_SEGMENT_CHARS) {
        const unsigned char *p = (const unsigned char *)text + offset;
        uint32_t cp = p[0];
        size_t width = 1;
        if (cp >= 0xc2 && cp <= 0xdf) { width = 2; cp &= 0x1f; }
        else if (cp >= 0xe0 && cp <= 0xef) { width = 3; cp &= 0x0f; }
        else if (cp >= 0xf0 && cp <= 0xf4) { width = 4; cp &= 0x07; }
        else if (cp >= 0x80) return 0;
        for (size_t i = 1; i < width; ++i) {
            if ((p[i] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (p[i] & 0x3f);
        }
        if ((width == 2 && cp < 0x80) || (width == 3 && cp < 0x800) ||
            (width == 4 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff)) return 0;
        offset += width;
        ++count;
        if (cp == '.' || cp == '!' || cp == '?' || cp == ';' || cp == '\n' ||
            cp == 0x3002 || cp == 0xff01 || cp == 0xff1f || cp == 0xff1b) {
            sentence = offset;
        }
        if (cp == ' ' || cp == '\t') word = offset;
    }
    if (!text[offset]) return offset;
    return sentence ? sentence : (word ? word : offset);
}
