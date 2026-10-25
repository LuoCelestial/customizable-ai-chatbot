#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "assistant_settings.h"

int main(void)
{
    assistant_settings_t s;
    assistant_settings_defaults(&s);
    assert(!strcmp(s.client.profile.name, "洛天依"));
    assert(s.client.profile.theme_color == 0x66ccff);
    assert(s.client.volume == 80);
    assert(!assistant_settings_valid(&s));
    strcpy(s.ssid, "test-network");
    strcpy(s.client.gateway_url, "http://192.168.1.20:8765");
    strcpy(s.client.gateway_token, "test-only-token-not-a-secret-12345678");
    assert(assistant_settings_valid(&s));
    strcpy(s.wifi_password, "short");
    assert(!assistant_settings_valid(&s));
    strcpy(s.wifi_password, "test-password");
    assert(assistant_settings_valid(&s));
    strcpy(s.backup.ssid, "backup-network");
    strcpy(s.backup.password, "backup-password");
    assert(assistant_settings_valid(&s));
    strcpy(s.backup.password, "short");
    assert(!assistant_wifi_valid(&s));
    s.backup.password[0] = 0; /* Open backup network. */
    assert(assistant_wifi_valid(&s));
    strcpy(s.backup.ssid, s.ssid);
    assert(!assistant_wifi_valid(&s));
    memset(&s.backup, 0, sizeof(s.backup));
    strcpy(s.backup.password, "orphan-password");
    assert(!assistant_wifi_valid(&s));
    memset(&s.backup, 0, sizeof(s.backup));
    strcpy(s.client.gateway_url, "http://host/path?query=value");
    assert(!assistant_settings_valid(&s));
    s.client.mode = ASSISTANT_MODE_DIRECT;
    strcpy(s.client.tencent_secret_id, "test-id");
    strcpy(s.client.asr_key, "test-placeholder");
    strcpy(s.client.deepseek_key, "test-placeholder");
    assert(assistant_settings_valid(&s));
    s.client.tencent_secret_id[0] = 0;
    assert(!assistant_settings_valid(&s));
    strcpy(s.client.tencent_secret_id, "test-id");
    strcpy(s.client.asr_model, "bad\"model");
    assert(!assistant_settings_valid(&s));
    strcpy(s.client.asr_model, ASSISTANT_DEFAULT_ASR_MODEL);
    strcpy(s.client.asr_url, "http://provider.invalid/v1/audio/transcriptions");
    assert(!assistant_settings_valid(&s));
    strcpy(s.client.asr_url, ASSISTANT_DEFAULT_ASR_URL);
    strcpy(s.client.asr_key, "key\r\nInjected:yes");
    assert(!assistant_settings_valid(&s));
    strcpy(s.client.asr_key, "test-placeholder");
    s.client.profile.persona = ASSISTANT_PERSONA_COUNT;
    assert(!assistant_settings_valid(&s));
    s.client.profile.persona = ASSISTANT_PERSONA_EXPERT;
    strcpy(s.client.profile.name, "   ");
    assert(!assistant_settings_valid(&s));
    strcpy(s.client.profile.name, "Expert");
    s.client.profile.theme_color = 0xff66b3;
    assert(assistant_settings_valid(&s));
    // 16 Chinese characters (48 UTF-8 bytes)
    strcpy(s.client.profile.name, "一二三四五六七八九十一二三四五六");
    assert(assistant_settings_valid(&s));
    memset(s.ssid, 'x', sizeof(s.ssid));
    assert(!assistant_settings_valid(&s));
    strcpy(s.ssid, "wifi"); s.version = 100;
    assert(!assistant_settings_valid(&s));
    s.version = 1;
    s.client.volume = 101;
    assert(!assistant_settings_valid(&s));
    s.client.volume = 80;
    assert(assistant_settings_valid(&s));
    puts("Assistant settings tests: PASS");
}
