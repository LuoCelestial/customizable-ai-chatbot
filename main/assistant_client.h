#pragma once

#include "assistant_memory.h"
#include "assistant_profile.h"
#include "assistant_tencent.h"
#include "esp_err.h"

#define ASSISTANT_SYSTEM_PROMPT_BYTES 512
#define ASSISTANT_TRANSCRIPT_BYTES 768
#define ASSISTANT_REPLY_BYTES 1536
#define ASSISTANT_DEFAULT_ASR_URL ASSISTANT_TENCENT_URL
#define ASSISTANT_DEFAULT_ASR_MODEL "16k_zh"
#define ASSISTANT_DEFAULT_DEEPSEEK_URL "https://api.deepseek.com/chat/completions"
#define ASSISTANT_DEFAULT_DEEPSEEK_MODEL "deepseek-flash"

typedef enum {
    ASSISTANT_MODE_GATEWAY = 0,
    ASSISTANT_MODE_DIRECT = 1,
} assistant_mode_t;

typedef struct {
    assistant_mode_t mode;
    char gateway_url[256]; /* Base URL, without /v1/voice. */
    char gateway_token[128];
    char asr_url[256];
    char asr_model[96];
    char asr_key[256];
    char deepseek_url[256];
    char deepseek_model[96];
    char deepseek_key[256];
    char tencent_secret_id[65]; /* Appended for legacy NVS compatibility; asr_key is SecretKey. */
    assistant_profile_t profile; /* Appended so older saved connection settings remain readable. */
    bool tts_enabled; /* Appended for TTS voice playback switch */
    char system_prompt[ASSISTANT_SYSTEM_PROMPT_BYTES]; /* Custom system prompt */
    uint8_t volume; /* Appended: playback volume 0..100 */
} assistant_client_config_t;

typedef enum {
    ASSISTANT_STAGE_CONNECTING,
    ASSISTANT_STAGE_RECORDING,
    ASSISTANT_STAGE_TRANSCRIBING,
    ASSISTANT_STAGE_THINKING,
} assistant_client_stage_t;

typedef struct {
    char transcript[ASSISTANT_TRANSCRIPT_BYTES + 1];
    char reply[ASSISTANT_REPLY_BYTES + 1];
    char error[128]; /* Sanitized local explanation, never a provider body. */
    unsigned turns;
    bool memory_saved;
    bool reply_truncated;
} assistant_client_result_t;

/* Fill exactly bytes with little-endian PCM; zero padding on stop is permitted.
 * Called synchronously on the caller's worker task. No LVGL lock may be held. */
typedef esp_err_t (*assistant_pcm_read_fn)(void *context, void *pcm, size_t bytes);
typedef void (*assistant_stage_fn)(void *context, assistant_client_stage_t stage);

/* All APIs are synchronous and must be serialized on one worker task. NVS must
 * already be initialized; HTTPS callers must synchronize the clock first.
 * No retries (avoids duplicate provider charges and duplicate turns). */
esp_err_t assistant_client_init(void);
unsigned assistant_client_memory_turns(void);
esp_err_t assistant_client_voice(const assistant_client_config_t *config,
                                 size_t pcm_bytes, assistant_pcm_read_fn read_pcm,
                                 void *pcm_context, assistant_stage_fn stage,
                                 void *stage_context, assistant_client_result_t *result);
/* Clears only the selected mode's memory. Gateway requires a live connection. */
esp_err_t assistant_client_clear_memory(const assistant_client_config_t *config,
                                        assistant_client_result_t *result);
