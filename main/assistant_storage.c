#include "assistant_setup.h"

#include <string.h>
#include <stddef.h>
#include "cJSON.h"
#include "nvs.h"

extern const uint8_t preset_start[] __asm__("_binary_assistant_wifi_preset_json_start");

bool assistant_wifi_preset_apply(assistant_settings_t *settings, const char *text)
{
    cJSON *json = cJSON_ParseWithOpts(text, NULL, true);
    const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(json, "ssid");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(json, "password");
    bool valid = settings && cJSON_IsObject(json) && cJSON_IsString(ssid) &&
                 cJSON_IsString(password) && strlen(ssid->valuestring) > 0 &&
                 strlen(ssid->valuestring) < sizeof(settings->ssid) &&
                 strlen(password->valuestring) < sizeof(settings->wifi_password);
    if (valid) {
        size_t length = strlen(password->valuestring);
        valid = length == 0 || (length >= 8 && length <= 63);
    }
    if (valid) {
        strcpy(settings->ssid, ssid->valuestring);
        strcpy(settings->wifi_password, password->valuestring);
    }
    cJSON_Delete(json);
    return valid;
}

esp_err_t assistant_settings_save(const assistant_settings_t *settings)
{
    if (!assistant_wifi_valid(settings) || !assistant_profile_valid(&settings->client.profile))
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("voice_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, "settings", settings, sizeof(*settings));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t assistant_settings_load(assistant_settings_t *settings)
{
    assistant_settings_defaults(settings);
    nvs_handle_t handle;
    esp_err_t err = nvs_open("voice_config", NVS_READONLY, &handle);
    if (err == ESP_OK) {
        size_t size = sizeof(*settings);
        err = nvs_get_blob(handle, "settings", settings, &size);
        nvs_close(handle);
        const size_t before_tencent = offsetof(assistant_settings_t, client.tencent_secret_id);
        const size_t before_profile = offsetof(assistant_settings_t, client.profile);
        const size_t before_tts = offsetof(assistant_settings_t, client.tts_enabled);
        const size_t before_prompt = offsetof(assistant_settings_t, client.system_prompt);
        const size_t before_volume = offsetof(assistant_settings_t, client.volume);
        const size_t before_backup = offsetof(assistant_settings_t, backup);
        bool known_size = size == sizeof(*settings) || size == before_backup || size == before_volume || size == before_prompt || size == before_tts || size == before_profile || size == before_tencent;
        if (err == ESP_OK) {
            if (!known_size || !assistant_wifi_valid(settings)) {
                err = ESP_ERR_INVALID_STATE;
            } else {
                if (size == before_tencent) settings->client.tencent_secret_id[0] = '\0';
                if (size == before_tencent || size == before_profile) {
                    assistant_profile_defaults(&settings->client.profile);
                }
                if (size == before_tencent || size == before_profile || size == before_tts) {
                    settings->client.tts_enabled = true;
                }
                if (size == before_tencent || size == before_profile || size == before_tts || size == before_prompt) {
                    settings->client.system_prompt[0] = '\0';
                }
                if (size == before_tencent || size == before_profile || size == before_tts || size == before_prompt || size == before_volume) {
                    settings->client.volume = 80;
                }
                if (!assistant_profile_valid(&settings->client.profile)) {
                    err = ESP_ERR_INVALID_STATE;
                }
            }
        }
    }
    if (err == ESP_OK) return ESP_OK;
    assistant_settings_defaults(settings);
    // Existing/corrupt data wins over a factory preset; never overwrite it to recover.
    if (err != ESP_ERR_NVS_NOT_FOUND) return err;
    if (!assistant_wifi_preset_apply(settings, (const char *)preset_start)) return ESP_ERR_NVS_NOT_FOUND;
    err = assistant_settings_save(settings);
    if (err != ESP_OK) assistant_settings_defaults(settings);
    return err;
}

esp_err_t assistant_settings_request_setup(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("voice_config", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, "setup", 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool assistant_settings_take_setup_request(void)
{
    nvs_handle_t handle;
    if (nvs_open("voice_config", NVS_READWRITE, &handle) != ESP_OK) return false;
    uint8_t requested = 0;
    nvs_get_u8(handle, "setup", &requested);
    if (requested) {
        nvs_set_u8(handle, "setup", 0);
        nvs_commit(handle);
    }
    nvs_close(handle);
    return requested != 0;
}
