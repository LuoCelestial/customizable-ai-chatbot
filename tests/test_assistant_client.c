/* Exercise the real client against HTTP/NVS faults; never contact a network. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assistant_client.h"
#include "esp_http_client.h"
#include "nvs.h"
#include "cJSON.h"

/* The fault tests inject a prompt symbol; firmware embeds the shared text file. */
const unsigned char persona[] __asm__("_binary_assistant_persona_txt_start") = "test-only persona";
const unsigned char companion[] __asm__("_binary_assistant_persona_companion_txt_start") = "test-only companion";
const unsigned char expert[] __asm__("_binary_assistant_persona_expert_txt_start") = "test-only expert";
const unsigned char search_prompt[] __asm__("_binary_assistant_search_prompt_txt_start") = "test-only search planner";
const char *assistant_weather_get_cached(void) { return "unknown"; }

struct mock_http { unsigned index; size_t read; int expected; size_t written; };
typedef struct { const char *body; int status; bool incomplete, fail_write; } response_t;
static response_t s_responses[4];
static unsigned s_created, s_closed;
static bool s_tencent;
static unsigned s_tc_headers;
static char s_payload[4][10000];
static bool s_stored, s_fail_commit;
static assistant_memory_t s_persisted, s_pending;
static unsigned s_commits;

int esp_crt_bundle_attach(void *configuration) { (void)configuration; return 0; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "assistant_mem") == 0);
    if (mode == NVS_READONLY && !s_stored) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *bytes)
{
    (void)h; assert(strcmp(key, "recent") == 0);
    if (!s_stored) return ESP_ERR_NVS_NOT_FOUND;
    assert(*bytes >= sizeof(s_persisted));
    memcpy(out, &s_persisted, sizeof(s_persisted)); *bytes = sizeof(s_persisted); return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t bytes)
{
    (void)h; assert(strcmp(key, "recent") == 0); assert(bytes == sizeof(s_pending));
    memcpy(&s_pending, data, bytes); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (s_fail_commit) return ESP_FAIL;
    s_persisted = s_pending; s_stored = true; ++s_commits; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    assert(config->crt_bundle_attach && (config->disable_auto_redirect || config->method == HTTP_METHOD_GET));
    assert(s_created < 4);
    struct mock_http *client = calloc(1, sizeof(*client)); assert(client);
    client->index = s_created++; return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *key, const char *value)
{
    (void)c;
    if (strcmp(key, "Authorization") == 0) {
        assert(strncmp(value, s_tencent && c->index == 0 ? "TC3-HMAC-SHA256 " : "Bearer ",
                       s_tencent && c->index == 0 ? 16 : 7) == 0);
    }
    if (!strcmp(key, "X-TC-Action")) { assert(!strcmp(value, "SentenceRecognition")); ++s_tc_headers; }
    if (!strcmp(key, "X-TC-Version")) { assert(!strcmp(value, "2019-06-14")); ++s_tc_headers; }
    if (!strcmp(key, "X-TC-Content-SHA256")) { assert(!strcmp(value, "UNSIGNED-PAYLOAD")); ++s_tc_headers; }
    if (!strcmp(key, "X-TC-Timestamp")) { assert(atoll(value) > 1735689600); ++s_tc_headers; }
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int bytes) { c->expected = bytes; return ESP_OK; }
int esp_http_client_write(esp_http_client_handle_t c, const char *data, int count)
{
    if (s_responses[c->index].fail_write) return -1;
    if (count > 17) count = 17; /* Network may accept only a partial write. */
    assert(c->written + (size_t)count < sizeof(s_payload[0]));
    memcpy(s_payload[c->index] + c->written, data, count); c->written += count; return count;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    assert(c->written == (size_t)c->expected);
    return (int64_t)strlen(s_responses[c->index].body);
}
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return s_responses[c->index].status; }
int esp_http_client_read(esp_http_client_handle_t c, char *out, int count)
{
    size_t remaining = strlen(s_responses[c->index].body) - c->read;
    if ((size_t)count > remaining) count = (int)remaining;
    if (count > 19) count = 19;
    memcpy(out, s_responses[c->index].body + c->read, count); c->read += count; return count;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c)
{
    /* Model the HTTP parser marking a full body complete before the client has
       consumed its cached bytes. A response must still be read and parsed. */
    return !s_responses[c->index].incomplete;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c) { ++s_closed; free(c); return ESP_OK; }

static void responses(const char *first, const char *second)
{
    memset(s_responses, 0, sizeof(s_responses)); memset(s_payload, 0, sizeof(s_payload));
    s_created = s_closed = 0;
    s_responses[0] = (response_t){.body = first, .status = 200};
    s_responses[1] = (response_t){.body = "{\"choices\":[{\"message\":{\"content\":\"{\\\"query\\\":\\\"green tea\\\"}\"},\"finish_reason\":\"stop\"}]}", .status = 200};
    s_responses[2] = (response_t){.body = "<rss><channel></channel></rss>", .status = 200};
    s_responses[3] = (response_t){.body = second, .status = 200};
}
static esp_err_t audio(void *ctx, void *out, size_t bytes) { (void)ctx; memset(out, 0, bytes); return ESP_OK; }
static assistant_client_config_t config(bool direct)
{
    assistant_client_config_t c = {0}; c.mode = direct ? ASSISTANT_MODE_DIRECT : ASSISTANT_MODE_GATEWAY;
    strcpy(c.gateway_url, "http://127.0.0.1:8765"); strcpy(c.gateway_token, "test-only-gateway-token-123456789012345");
    strcpy(c.asr_url, "https://asr.invalid/audio/transcriptions"); strcpy(c.asr_key, "test-placeholder"); strcpy(c.asr_model, "test-model");
    strcpy(c.deepseek_url, "https://chat.invalid/chat/completions"); strcpy(c.deepseek_key, "test-placeholder"); strcpy(c.deepseek_model, "test-model");
    assistant_profile_defaults(&c.profile);
    return c;
}
static esp_err_t voice(assistant_client_config_t *c, assistant_client_result_t *r)
{
    esp_err_t error = assistant_client_voice(c, 64, audio, NULL, NULL, NULL, r);
    assert(s_created == s_closed); return error;
}

int main(void)
{
    assistant_client_result_t result;
    assistant_client_config_t c = config(true);
    const char *asr = "{\"text\":\"Remember green tea\"}";
    const char *chat = "{\"choices\":[{\"message\":{\"content\":\"I will remember green tea\"},\"finish_reason\":\"stop\"}]}";
    assert(assistant_client_init() == ESP_OK);
    responses(asr, chat);
    assert(voice(&c, &result) == ESP_OK && result.memory_saved);
    assert(s_created == 4 && s_commits == 1 && s_persisted.count == 1);
    cJSON *payload = cJSON_Parse(s_payload[3]); assert(payload);
    assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(payload, "thinking"), "type")->valuestring, "disabled"));
    cJSON *system = cJSON_GetArrayItem(cJSON_GetObjectItem(payload, "messages"), 0);
    assert(!strcmp(cJSON_GetObjectItem(system, "role")->valuestring, "system"));
    assert(!strcmp(cJSON_GetObjectItem(system, "content")->valuestring, "test-only persona"));
    cJSON *identity = cJSON_GetArrayItem(cJSON_GetObjectItem(payload, "messages"), 1);
    assert(strstr(cJSON_GetObjectItem(identity, "content")->valuestring, "洛天依"));
    cJSON_Delete(payload);
    assert(strstr(s_payload[1], "test-only search planner"));
    assert(!strstr(s_payload[1], "test-only persona"));
    assert(assistant_client_init() == ESP_OK && assistant_client_memory_turns() == 1);
    responses(asr, chat); s_fail_commit = true;
    assert(voice(&c, &result) == ESP_OK && !result.memory_saved);
    assert(assistant_client_memory_turns() == 1 && s_persisted.count == 1);
    s_fail_commit = false;
    assert(strstr(s_payload[3], "I will remember green tea"));
    responses(asr, "invalid json");
    assert(voice(&c, &result) != ESP_OK && s_persisted.count == 1);
    responses(asr, chat); s_responses[0].status = 401;
    assert(voice(&c, &result) != ESP_OK && s_created == 1 && s_commits == 1);
    responses(asr, chat); s_responses[0].status = 302;
    assert(voice(&c, &result) != ESP_OK && s_created == 1);
    responses(asr, chat); s_responses[0].fail_write = true;
    assert(voice(&c, &result) != ESP_OK && s_created == 1);
    responses(asr, chat); s_responses[0].incomplete = true;
    assert(voice(&c, &result) != ESP_OK && s_created == 1);
    s_persisted.version = 999;
    assert(assistant_client_init() != ESP_OK);
    responses(asr, chat);
    assert(voice(&c, &result) != ESP_OK && s_created == 0);
    assert(assistant_client_clear_memory(&c, &result) == ESP_OK && result.memory_saved);
    assert(s_persisted.count == 0 && assistant_client_memory_turns() == 0);
    c = config(false);
    responses("{\"transcript\":\"你好\",\"reply\":\"记住了\",\"turns\":7}", "");
    assert(voice(&c, &result) == ESP_OK && result.memory_saved && result.turns == 7);
    responses("{\"transcript\":\"hello\",\"reply\":\"reply\",\"turns\":-1}", "");
    assert(voice(&c, &result) != ESP_OK);
    responses("{\"ok\":true,\"turns\":0}", "");
    assert(assistant_client_clear_memory(&c, &result) == ESP_OK && result.turns == 0);
    assert(s_created == s_closed);
    char auth[400];
    assert(assistant_tencent_authorization("test-id", "test-secret-key", 1767225600, auth, sizeof(auth)));
    assert(!strcmp(auth, "TC3-HMAC-SHA256 Credential=test-id/2026-01-01/asr/tc3_request, SignedHeaders=content-type;host, Signature=efb1f9860d77b6bcee15739ce325e582951556c41abc9c861a0b995a31ab720c"));
    assert(!assistant_tencent_authorization("id", "key", 0, auth, sizeof(auth)));
    assert(!assistant_tencent_authorization("id", "key", 1767225600, auth, 8));
    char encoded[16];
    assert(assistant_base64((const uint8_t *)"f", 1, encoded, sizeof(encoded)) == 4 && !strcmp(encoded, "Zg=="));
    assert(assistant_base64((const uint8_t *)"fo", 2, encoded, sizeof(encoded)) == 4 && !strcmp(encoded, "Zm8="));
    assert(assistant_base64((const uint8_t *)"foo", 3, encoded, sizeof(encoded)) == 4 && !strcmp(encoded, "Zm9v"));
    assert(!assistant_base64((const uint8_t *)"foo", 3, encoded, 4));
    c = config(true); s_tencent = true;
    strcpy(c.asr_url, ASSISTANT_TENCENT_URL); strcpy(c.asr_model, "16k_zh");
    strcpy(c.tencent_secret_id, "test-id");
    responses("{\"Response\":{\"Result\":\"Hello Tencent\"}}", chat);
    assert(voice(&c, &result) == ESP_OK && s_tc_headers == 4);
    payload = cJSON_Parse(s_payload[0]); assert(payload);
    assert(!strcmp(cJSON_GetObjectItem(payload, "EngSerViceType")->valuestring, "16k_zh"));
    assert(!strcmp(cJSON_GetObjectItem(payload, "VoiceFormat")->valuestring, "pcm"));
    assert(cJSON_GetObjectItem(payload, "DataLen")->valueint == 64);
    const char *data = cJSON_GetObjectItem(payload, "Data")->valuestring;
    assert(strlen(data) == 88 && !strcmp(data + 86, "=="));
    for (unsigned i = 0; i < 86; ++i) assert(data[i] == 'A');
    cJSON_Delete(payload);
    responses("{\"Response\":{\"Result\":\"Hello\"}}", chat);
    assert(assistant_client_voice(&c, 1538, audio, NULL, NULL, NULL, &result) == ESP_OK);
    payload = cJSON_Parse(s_payload[0]); assert(payload);
    data = cJSON_GetObjectItem(payload, "Data")->valuestring;
    assert(strlen(data) == 2052 && data[2051] == '=');
    for (unsigned i = 0; i < 2051; ++i) assert(data[i] == 'A');
    cJSON_Delete(payload);
    unsigned before = s_commits;
    responses("{\"Response\":{\"Error\":{\"Code\":\"AuthFailure\",\"Message\":\"PRIVATE\"}}}", chat);
    assert(voice(&c, &result) != ESP_OK && s_created == 1 && s_commits == before);
    assert(!strstr(result.error, "PRIVATE"));
    responses("{\"Response\":{\"Result\":\" \"}}", chat);
    assert(voice(&c, &result) != ESP_OK && s_created == 1);
    c.tencent_secret_id[0] = 0;
    responses("{}", chat);
    assert(voice(&c, &result) != ESP_OK && s_created == 0);
    c = config(true); s_tencent = false;
    responses(asr, chat);
    s_responses[1].body = "{\"choices\":[{\"message\":{\"content\":\"{\\\"query\\\":\\\"\\\"}\"},\"finish_reason\":\"stop\"}]}";
    s_responses[2] = s_responses[3];
    assert(voice(&c, &result) == ESP_OK && s_created == 3);
    assert(strstr(s_payload[2], "无需检索"));
    responses(asr, chat);
    s_responses[1].body = "{\"choices\":[{\"message\":{\"content\":\"not-json\"},\"finish_reason\":\"stop\"}]}";
    s_responses[2] = s_responses[3];
    assert(voice(&c, &result) == ESP_OK && s_created == 3);
    assert(strstr(s_payload[2], "规划失败"));
    puts("Assistant client HTTP/NVS/TC3 tests: PASS");
    return 0;
}
