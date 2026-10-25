#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "assistant_setup.h"
#include "nvs.h"

const unsigned char preset[] __asm__("_binary_assistant_wifi_preset_json_start") =
    "{\"ssid\":\"test-only-factory-network\",\"password\":\"test-password\"}";
static bool stored, fail_commit, pending_blob;
static assistant_settings_t data, pending;
static unsigned commits;
static size_t stored_bytes = sizeof(assistant_settings_t);
static uint8_t setup;

esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "voice_config") == 0);
    if (mode == NVS_READONLY && !stored) return ESP_ERR_NVS_NOT_FOUND;
    pending_blob = false; *handle = 1; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *size)
{
    (void)h; assert(!strcmp(key, "settings"));
    if (!stored) return ESP_ERR_NVS_NOT_FOUND;
    assert(*size >= stored_bytes); memcpy(out, &data, stored_bytes); *size = stored_bytes; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t size)
{
    (void)h; assert(!strcmp(key, "settings")); assert(size == sizeof(pending));
    memcpy(&pending, value, size); pending_blob = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fail_commit) return ESP_FAIL;
    if (pending_blob) { data = pending; stored = true; }
    ++commits; return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *value)
{ (void)h; assert(!strcmp(key, "setup")); *value = setup; return ESP_OK; }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{ (void)h; assert(!strcmp(key, "setup")); setup = value; return ESP_OK; }

int main(void)
{
    assistant_settings_t settings;
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(assistant_wifi_valid(&settings) && !assistant_settings_valid(&settings));
    assert(stored && commits == 1);
    assert(!strcmp(settings.ssid, "test-only-factory-network"));
    strcpy(settings.ssid, "user-changed-network");
    strcpy(settings.client.deepseek_key, "test-preserved-api-key");
    strcpy(settings.backup.ssid, "test-only-backup-network");
    strcpy(settings.backup.password, "test-backup-password");
    assert(assistant_settings_save(&settings) == ESP_OK);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(!strcmp(settings.ssid, "user-changed-network"));
    assert(!strcmp(settings.client.deepseek_key, "test-preserved-api-key"));
    assert(!strcmp(settings.backup.ssid, "test-only-backup-network"));
    assert(!strcmp(settings.backup.password, "test-backup-password"));
    assert(commits == 2); /* Factory preset never overwrites user settings. */
    stored_bytes = offsetof(assistant_settings_t, backup);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(!settings.backup.ssid[0] && !settings.backup.password[0]);
    assert(!strcmp(settings.client.deepseek_key, "test-preserved-api-key"));
    assert(settings.client.volume == 80 && commits == 2);
    /* Blob without volume. */
    stored_bytes = offsetof(assistant_settings_t, client.volume);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(settings.client.volume == 80);
    /* Blob without system prompt. */
    stored_bytes = offsetof(assistant_settings_t, client.system_prompt);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(settings.client.system_prompt[0] == '\0');
    /* Blob without TTS switch. */
    stored_bytes = offsetof(assistant_settings_t, client.tts_enabled);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(settings.client.tts_enabled == true);
    /* Previous full blobs ended immediately before the appended profile. */
    stored_bytes = offsetof(assistant_settings_t, client.profile);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(!strcmp(settings.client.profile.name, "洛天依"));
    assert(settings.client.profile.theme_color == 0x66ccff);
    assert(settings.client.tts_enabled == true);
    /* Legacy blobs end immediately before the appended Tencent ID. */
    stored_bytes = offsetof(assistant_settings_t, client.tencent_secret_id);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(!strcmp(settings.ssid, "user-changed-network"));
    assert(!strcmp(settings.client.deepseek_key, "test-preserved-api-key"));
    assert(settings.client.tencent_secret_id[0] == 0 && commits == 2);
    assert(!strcmp(settings.client.profile.name, "洛天依"));
    assert(settings.client.profile.theme_color == 0x66ccff);
    stored_bytes -= 4;
    assert(assistant_settings_load(&settings) == ESP_ERR_INVALID_STATE);
    stored_bytes = sizeof(data);
    assert(assistant_settings_load(&settings) == ESP_OK);
    assistant_settings_t original = settings;
    assert(!assistant_wifi_preset_apply(&settings, "{\"ssid\":\"bad\",\"password\":\"short\"}"));
    assert(!memcmp(&settings, &original, sizeof(settings)));
    assert(!assistant_wifi_preset_apply(&settings, "{}"));
    assert(!assistant_wifi_preset_apply(&settings, "not json"));
    assert(assistant_wifi_preset_apply(&settings, "{\"ssid\":\"open-network\",\"password\":\"\"}"));
    assert(assistant_wifi_valid(&settings));
    data.version = 999;
    unsigned previous = commits;
    assert(assistant_settings_load(&settings) == ESP_ERR_INVALID_STATE);
    assert(commits == previous && data.version == 999);
    stored = false; fail_commit = true;
    assert(assistant_settings_load(&settings) == ESP_FAIL);
    assert(!assistant_wifi_valid(&settings) && !stored);
    fail_commit = false;
    assert(assistant_settings_load(&settings) == ESP_OK);
    assert(assistant_settings_request_setup() == ESP_OK);
    assert(assistant_settings_take_setup_request());
    assert(!assistant_settings_take_setup_request());
    puts("Wi-Fi preset import, persistence and failure tests: PASS");
}
