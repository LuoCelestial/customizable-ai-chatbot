#include "assistant_settings.h"

#include <string.h>

static bool terminated(const char *s, size_t capacity)
{
    return memchr(s, '\0', capacity) != NULL;
}

static bool printable(const char *s, bool empty)
{
    if (!empty && !*s) return false;
    for (; *s; ++s) if ((unsigned char)*s < 33 || (unsigned char)*s > 126) return false;
    return true;
}

static bool endpoint(const char *url, bool allow_http)
{
    if (!printable(url, false)) return false;
    const char *host = NULL;
    if (strncmp(url, "https://", 8) == 0) host = url + 8;
    else if (allow_http && strncmp(url, "http://", 7) == 0) host = url + 7;
    if (!host || !*host || *host == '/' || *host == ':') return false;
    if (strpbrk(host, "@?#\\\"<>")) return false;
    return true;
}

void assistant_settings_defaults(assistant_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    s->version = 1;
    s->client.mode = ASSISTANT_MODE_GATEWAY;
    assistant_profile_defaults(&s->client.profile);
    s->client.tts_enabled = true;
    s->client.volume = 80;
    strcpy(s->client.asr_url, ASSISTANT_DEFAULT_ASR_URL);
    strcpy(s->client.asr_model, ASSISTANT_DEFAULT_ASR_MODEL);
    strcpy(s->client.deepseek_url, ASSISTANT_DEFAULT_DEEPSEEK_URL);
    strcpy(s->client.deepseek_model, ASSISTANT_DEFAULT_DEEPSEEK_MODEL);
}

bool assistant_wifi_valid(const assistant_settings_t *s)
{
    if (!s || s->version != 1 || !terminated(s->ssid, sizeof(s->ssid)) ||
        !terminated(s->wifi_password, sizeof(s->wifi_password)) || !s->ssid[0]) return false;
    size_t length = strlen(s->wifi_password);
    return (length == 0 || (length >= 8 && length <= 63)) &&
           assistant_backup_wifi_valid(&s->backup) &&
           (!s->backup.ssid[0] || strcmp(s->ssid, s->backup.ssid) != 0);
}

bool assistant_backup_wifi_valid(const assistant_backup_wifi_t *backup)
{
    if (!backup || !terminated(backup->ssid, sizeof(backup->ssid)) ||
        !terminated(backup->password, sizeof(backup->password))) return false;
    size_t length = strlen(backup->password);
    if (!backup->ssid[0]) return length == 0;
    return length == 0 || (length >= 8 && length <= 63);
}

bool assistant_settings_valid(const assistant_settings_t *s)
{
    if (!assistant_wifi_valid(s)) return false;
#define BOUNDED(field) if (!terminated(s->field, sizeof(s->field))) return false
    BOUNDED(ssid); BOUNDED(wifi_password);
    BOUNDED(client.gateway_url); BOUNDED(client.gateway_token);
    BOUNDED(client.asr_url); BOUNDED(client.asr_model); BOUNDED(client.asr_key);
    BOUNDED(client.tencent_secret_id);
    BOUNDED(client.deepseek_url); BOUNDED(client.deepseek_model); BOUNDED(client.deepseek_key);
#undef BOUNDED
    if (!assistant_profile_valid(&s->client.profile)) return false;
    if (s->client.volume > 100) return false;
    if (s->client.mode == ASSISTANT_MODE_GATEWAY) {
        return endpoint(s->client.gateway_url, true) &&
               strlen(s->client.gateway_token) >= 32 && printable(s->client.gateway_token, false);
    }
    if (s->client.mode != ASSISTANT_MODE_DIRECT) return false;
    if (!strcmp(s->client.asr_url, ASSISTANT_TENCENT_URL) &&
        (!assistant_tencent_identifier(s->client.tencent_secret_id, sizeof(s->client.tencent_secret_id)) ||
         !assistant_tencent_identifier(s->client.asr_model, sizeof(s->client.asr_model)))) return false;
    return endpoint(s->client.asr_url, false) && endpoint(s->client.deepseek_url, false) &&
           printable(s->client.asr_model, false) && printable(s->client.deepseek_model, false) &&
           printable(s->client.asr_key, false) && printable(s->client.deepseek_key, false);
}
