#include "assistant_tts.h"
#include "assistant_tencent.h"
#include "assistant_tts_text.h"
#include "bsp_audio.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "assistant_tts";

static inline int b64_char_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2; /* padding */
    return -1; /* invalid / whitespace */
}

static inline size_t decode_b64_quad(const int val[4], uint8_t out[3])
{
    if (val[0] < 0 || val[1] < 0) return 0;
    out[0] = (uint8_t)((val[0] << 2) | (val[1] >> 4));
    if (val[2] == -2) return 1;
    if (val[2] < 0) return 0;
    out[1] = (uint8_t)(((val[1] & 0x0F) << 4) | (val[2] >> 2));
    if (val[3] == -2) return 2;
    if (val[3] < 0) return 0;
    out[2] = (uint8_t)(((val[2] & 0x03) << 6) | val[3]);
    return 3;
}

static int select_voice_type(assistant_persona_t persona)
{
    switch (persona) {
        case ASSISTANT_PERSONA_LUO_TIANYI:
            return 101001; /* 智瑜 - 情感女声 / 适合元气歌姬 */
        case ASSISTANT_PERSONA_COMPANION:
            return 1001;   /* 智美 - 标准亲切女声 */
        case ASSISTANT_PERSONA_EXPERT:
            return 1002;   /* 智霖 - 成熟专业男声 */
        default:
            return 101001;
    }
}

typedef enum {
    TTS_PARSE_FIND_KEY,
    TTS_PARSE_FIND_COLON,
    TTS_PARSE_FIND_QUOTE,
    TTS_PARSE_DECODE_B64,
    TTS_PARSE_CAPTURE_ERROR,
    TTS_PARSE_DONE,
} tts_parse_state_t;

static bool playback_aborted(const atomic_bool *abort_flag)
{
    return abort_flag && atomic_load(abort_flag);
}

static esp_err_t speak_segment(const assistant_client_config_t *config,
                              const char *text,
                              const atomic_bool *abort_flag,
                              char *error_detail, size_t error_detail_size,
                              unsigned segment_index)
{
    if (!config || !text || !text[0]) return ESP_ERR_INVALID_ARG;
    if (strlen(config->tencent_secret_id) == 0 || strlen(config->asr_key) == 0) {
        ESP_LOGW(TAG, "Tencent credentials missing, cannot synthesize speech");
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Build JSON request */
    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    int voice_type = select_voice_type(config->profile.persona);
    char session_id[33];
    uint32_t now_sec = (uint32_t)time(NULL);
    snprintf(session_id, sizeof(session_id), "tts_%08lx%08x", (unsigned long)now_sec, segment_index);

    cJSON_AddStringToObject(root, "Text", text);
    cJSON_AddStringToObject(root, "SessionId", session_id);
    cJSON_AddNumberToObject(root, "ModelType", 1);
    cJSON_AddNumberToObject(root, "VoiceType", voice_type);
    cJSON_AddStringToObject(root, "Codec", "pcm");
    cJSON_AddNumberToObject(root, "SampleRate", 16000);
    cJSON_AddNumberToObject(root, "Volume", 1);

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) return ESP_ERR_NO_MEM;

    size_t payload_len = strlen(payload);

    /* Generate TC3 Authorization */
    int64_t timestamp = (int64_t)time(NULL);
    char auth[384];
    if (!assistant_tencent_tts_authorization(config->tencent_secret_id, config->asr_key,
                                             timestamp, auth, sizeof(auth))) {
        cJSON_free(payload);
        ESP_LOGE(TAG, "Failed to generate TTS authorization header (timestamp=%lld)", (long long)timestamp);
        return ESP_ERR_INVALID_STATE;
    }

    char stamp[24];
    snprintf(stamp, sizeof(stamp), "%lld", (long long)timestamp);

    esp_http_client_config_t http_cfg = {
        .url = ASSISTANT_TENCENT_TTS_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        cJSON_free(payload);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json; charset=utf-8");
    esp_http_client_set_header(client, "Host", ASSISTANT_TENCENT_TTS_HOST);
    esp_http_client_set_header(client, "X-TC-Action", "TextToVoice");
    esp_http_client_set_header(client, "X-TC-Version", "2019-08-23");
    esp_http_client_set_header(client, "X-TC-Timestamp", stamp);
    esp_http_client_set_header(client, "X-TC-Content-SHA256", "UNSIGNED-PAYLOAD");
    esp_http_client_set_header(client, "Authorization", auth);

    esp_err_t err = esp_http_client_open(client, (int)payload_len);
    if (err == ESP_OK) {
        int written = esp_http_client_write(client, payload, (int)payload_len);
        if (written < (int)payload_len) err = ESP_FAIL;
    }
    cJSON_free(payload);

    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGE(TAG, "HTTP open/write failed: %s", esp_err_to_name(err));
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Tencent TTS response: HTTP %d, Content-Length=%lld", status_code, (long long)content_length);

    if (status_code != 200) {
        if (error_detail && error_detail_size) {
            snprintf(error_detail, error_detail_size, "HTTP %d", status_code);
        }
        ESP_LOGW(TAG, "Tencent TTS returned non-200 HTTP status %d", status_code);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    /* Initialize audio format and volume explicitly */
    err = bsp_audio_set_format(16000, 16, 1);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    uint8_t vol = config->volume > 100 ? 80 : config->volume;
    bsp_audio_set_volume(vol);

    /* Stream parse response with resilient JSON state machine */
    tts_parse_state_t state = TTS_PARSE_FIND_KEY;
    const char *key_audio = "Audio";
    const char *key_error = "Error";
    size_t match_audio = 0;
    size_t match_error = 0;
    size_t target_key_len = 5;

    int quad_vals[4];
    int quad_count = 0;

    uint8_t pcm_buf[512];
    size_t pcm_used = 0;
    size_t total_pcm_bytes = 0;

    char err_buf[256];
    size_t err_used = 0;

    char chunk[1024];
    while (state != TTS_PARSE_DONE) {
        if (playback_aborted(abort_flag)) {
            ESP_LOGI(TAG, "TTS playback aborted by user");
            break;
        }

        int r = esp_http_client_read(client, chunk, sizeof(chunk));
        if (r <= 0) break;

        for (int i = 0; i < r && state != TTS_PARSE_DONE; ++i) {
            char c = chunk[i];

            switch (state) {
                case TTS_PARSE_FIND_KEY:
                    if (c == key_audio[match_audio]) {
                        match_audio++;
                        if (match_audio == target_key_len) {
                            state = TTS_PARSE_FIND_COLON;
                            break;
                        }
                    } else {
                        match_audio = (c == key_audio[0]) ? 1 : 0;
                    }

                    if (c == key_error[match_error]) {
                        match_error++;
                        if (match_error == target_key_len) {
                            state = TTS_PARSE_CAPTURE_ERROR;
                            break;
                        }
                    } else {
                        match_error = (c == key_error[0]) ? 1 : 0;
                    }
                    break;

                case TTS_PARSE_FIND_COLON:
                    if (c == ':') {
                        state = TTS_PARSE_FIND_QUOTE;
                    }
                    break;

                case TTS_PARSE_FIND_QUOTE:
                    if (c == '"') {
                        state = TTS_PARSE_DECODE_B64;
                    } else if (!isspace((unsigned char)c)) {
                        /* Unexpected token before quote, restart seeking */
                        state = TTS_PARSE_FIND_KEY;
                        match_audio = 0;
                    }
                    break;

                case TTS_PARSE_DECODE_B64:
                    if (c == '"') {
                        state = TTS_PARSE_DONE;
                        break;
                    }
                    {
                        int v = b64_char_value(c);
                        if (v == -1) continue; /* ignore whitespace / newlines */

                        quad_vals[quad_count++] = v;
                        if (quad_count == 4) {
                            uint8_t decoded[3];
                            size_t bytes = decode_b64_quad(quad_vals, decoded);
                            quad_count = 0;
                            for (size_t b = 0; b < bytes; ++b) {
                                pcm_buf[pcm_used++] = decoded[b];
                                total_pcm_bytes++;
                                if (pcm_used == sizeof(pcm_buf)) {
                                    if (playback_aborted(abort_flag)) {
                                        state = TTS_PARSE_DONE;
                                        break;
                                    }
                                    err = bsp_audio_write(pcm_buf, sizeof(pcm_buf));
                                    if (err != ESP_OK) goto cleanup;
                                    pcm_used = 0;
                                }
                            }
                        }
                    }
                    break;

                case TTS_PARSE_CAPTURE_ERROR:
                    if (err_used + 1 < sizeof(err_buf)) {
                        err_buf[err_used++] = c;
                    }
                    break;

                case TTS_PARSE_DONE:
                    break;
            }
        }
    }

    if (err_used > 0) {
        err_buf[err_used] = '\0';
        /* Keep only the service error code; never log returned text/secrets. */
        /* Parse just the Code value, even when a long Message was truncated. */
        char *field = strstr(err_buf, "\"Code\"");
        char *colon = field ? strchr(field + 6, ':') : NULL;
        cJSON *code = colon ? cJSON_Parse(colon + 1) : NULL;
        const char *name = cJSON_IsString(code) ? code->valuestring : "Tencent TTS error";
        ESP_LOGE(TAG, "Tencent TTS error code: %s", name);
        if (error_detail && error_detail_size) {
            snprintf(error_detail, error_detail_size, "%s", name);
        }
        cJSON_Delete(code);
        err = ESP_FAIL;
        goto cleanup;
    }

    if (playback_aborted(abort_flag)) goto cleanup;
    if (state != TTS_PARSE_DONE || quad_count != 0 || total_pcm_bytes == 0 ||
        (total_pcm_bytes & 1)) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    /* Flush remaining PCM buffer */
    if (pcm_used > 0) {
        err = bsp_audio_write(pcm_buf, pcm_used);
        if (err != ESP_OK) goto cleanup;
    }

    /* Zero-padding for smooth stop */
    uint8_t zero_padding[64] = {0};
    err = bsp_audio_write(zero_padding, sizeof(zero_padding));

    ESP_LOGI(TAG, "TTS complete, total decoded PCM: %u bytes", (unsigned)total_pcm_bytes);

cleanup:
    esp_http_client_cleanup(client);
    return err;
}

esp_err_t assistant_tts_speak(const assistant_client_config_t *config,
                            const char *text, const atomic_bool *abort_flag,
                            char *error_detail, size_t error_detail_size)
{
    if (error_detail && error_detail_size) error_detail[0] = '\0';
    esp_err_t err = ESP_OK;
    if (!config || !text || !text[0]) err = ESP_ERR_INVALID_ARG;
    unsigned index = 0;
    while (err == ESP_OK && *text && !playback_aborted(abort_flag)) {
        while (*text && isspace((unsigned char)*text)) ++text;
        if (!*text) break;
        size_t bytes = assistant_tts_segment_bytes(text);
        if (!bytes) { err = ESP_ERR_INVALID_ARG; break; }
        char segment[ASSISTANT_TTS_SEGMENT_BYTES + 1];
        memcpy(segment, text, bytes);
        segment[bytes] = '\0';
        ESP_LOGI(TAG, "Synthesizing segment %u (%u UTF-8 bytes)", ++index, (unsigned)bytes);
        err = speak_segment(config, segment, abort_flag, error_detail, error_detail_size, index);
        text += bytes;
    }
    if (err != ESP_OK && error_detail && error_detail_size && !error_detail[0]) {
        snprintf(error_detail, error_detail_size, "%s", esp_err_to_name(err));
    }
    return err;
}
