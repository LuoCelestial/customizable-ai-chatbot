#pragma once

#include "assistant_client.h"
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Synchronous streaming TTS playback.
 * Synthesizes text via Tencent Cloud TextToVoice (PCM 16k 16-bit mono),
 * streams base64 chunks directly into ES8311 I2S playback without buffering whole audio in RAM.
 * Splits long UTF-8 replies into bounded segments and plays them in order.
 * abort_flag: if non-null and set to true, stops between reads/audio writes.
 * error_detail: optional caller-owned buffer for the API code or local error.
 * Returns ESP_OK on success or stopped cleanly. */
esp_err_t assistant_tts_speak(const assistant_client_config_t *config,
                              const char *text,
                              const atomic_bool *abort_flag,
                              char *error_detail, size_t error_detail_size);

#ifdef __cplusplus
}
#endif
