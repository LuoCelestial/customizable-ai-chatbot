#include "assistant_setup.h"
#include "assistant_wallpaper.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"

static char s_csrf[33];
static void (*s_on_saved)(void);
static bool s_saved;
extern const uint8_t setup_html_start[] asm("_binary_setup_html_start");

static esp_err_t page(httpd_req_t *req)
{
    const char *html = (const char *)setup_html_start;
    const char *token = strstr(html, "{{SETUP_TOKEN}}");
    if (!token) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Page unavailable");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Content-Security-Policy", "default-src 'self'; img-src 'self' blob: data:; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'");
    esp_err_t err = httpd_resp_send_chunk(req, html, token - html);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, s_csrf, strlen(s_csrf));
    if (err == ESP_OK) err = httpd_resp_sendstr_chunk(req, token + strlen("{{SETUP_TOKEN}}"));
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

static bool read_field(cJSON *json, const char *key, char *out, size_t capacity)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= capacity) return false;
    strcpy(out, item->valuestring);
    return true;
}

static bool read_backup_wifi(cJSON *json, assistant_settings_t *settings)
{
    cJSON *enabled = cJSON_GetObjectItemCaseSensitive(json, "backup_enabled");
    /* Older setup pages do not know about backup Wi-Fi: preserve the value. */
    if (!enabled) return true;
    if (!cJSON_IsBool(enabled)) return false;
    if (!cJSON_IsTrue(enabled)) {
        memset(&settings->backup, 0, sizeof(settings->backup));
        return true;
    }
    cJSON *reuse = cJSON_GetObjectItemCaseSensitive(json, "use_saved_backup");
    if (!cJSON_IsBool(reuse)) return false;
    if (cJSON_IsTrue(reuse))
        return settings->backup.ssid[0] && assistant_backup_wifi_valid(&settings->backup);
    return read_field(json, "backup_ssid", settings->backup.ssid, sizeof(settings->backup.ssid)) &&
           read_field(json, "backup_wifi_password", settings->backup.password, sizeof(settings->backup.password)) &&
           settings->backup.ssid[0] && assistant_backup_wifi_valid(&settings->backup);
}

static bool read_theme(cJSON *json, uint32_t *color)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, "theme_color");
    if (!cJSON_IsString(item) || strlen(item->valuestring) != 7 || item->valuestring[0] != '#')
        return false;
    unsigned value = 0;
    for (size_t i = 1; i < 7; ++i) {
        char c = item->valuestring[i];
        unsigned digit = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                         c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) :
                         c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16;
        if (digit == 16) return false;
        value = value * 16 + digit;
    }
    *color = value;
    return true;
}

static bool read_profile(cJSON *json, assistant_profile_t *profile)
{
    cJSON *persona = cJSON_GetObjectItemCaseSensitive(json, "persona");
    return read_field(json, "assistant_name", profile->name, sizeof(profile->name)) &&
           cJSON_IsString(persona) &&
           assistant_persona_from_key(persona->valuestring, &profile->persona) &&
           read_theme(json, &profile->theme_color) && assistant_profile_valid(profile);
}

static bool authorized(httpd_req_t *req)
{
    char token[sizeof(s_csrf)];
    return httpd_req_get_hdr_value_str(req, "X-Setup-Token", token, sizeof(token)) == ESP_OK &&
           strcmp(token, s_csrf) == 0;
}

static esp_err_t json_response(httpd_req_t *req, const char *json)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t wifi_saved(httpd_req_t *req)
{
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    assistant_settings_t *settings = malloc(sizeof(*settings));
    if (!settings) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    bool saved = assistant_settings_load(settings) == ESP_OK;
    cJSON *json = cJSON_CreateObject();
    bool ok = json && cJSON_AddBoolToObject(json, "saved", saved) &&
              cJSON_AddStringToObject(json, "ssid", saved ? settings->ssid : "") &&
              cJSON_AddBoolToObject(json, "backup_enabled", saved && settings->backup.ssid[0]) &&
              cJSON_AddStringToObject(json, "backup_ssid", saved ? settings->backup.ssid : "");
    char *text = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    memset(settings, 0, sizeof(*settings));
    free(settings);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    esp_err_t err = json_response(req, text);
    cJSON_free(text);
    return err;
}

static esp_err_t profile_status(httpd_req_t *req)
{
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    assistant_settings_t *settings = malloc(sizeof(*settings));
    if (!settings) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    bool loaded = assistant_settings_load(settings) == ESP_OK;
    if (!loaded) assistant_settings_defaults(settings);
    cJSON *json = cJSON_CreateObject();
    char color[8];
    snprintf(color, sizeof(color), "#%06lx", (unsigned long)settings->client.profile.theme_color);
    const char *persona = assistant_persona_key(settings->client.profile.persona);
    bool ok = json && cJSON_AddStringToObject(json, "assistant_name", settings->client.profile.name) &&
              cJSON_AddStringToObject(json, "theme_color", color) &&
              cJSON_AddStringToObject(json, "persona", persona ? persona : "luotianyi") &&
              cJSON_AddBoolToObject(json, "tts_enabled", settings->client.tts_enabled) &&
              cJSON_AddNumberToObject(json, "volume", settings->client.volume) &&
              cJSON_AddStringToObject(json, "system_prompt", settings->client.system_prompt);
    char *text = ok ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
    memset(settings, 0, sizeof(*settings));
    free(settings);
    if (!text) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    esp_err_t err = json_response(req, text);
    cJSON_free(text);
    return err;
}

static esp_err_t profile_save(httpd_req_t *req)
{
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    if (s_saved) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Already saved; restarting");
    if (req->content_len <= 0 || req->content_len > 1536)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
    char *body = calloc(1, req->content_len + 1);
    assistant_settings_t *settings = malloc(sizeof(*settings));
    if (!body || !settings) {
        free(body); free(settings);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body); free(settings);
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timed out");
        }
        received += n;
    }
    cJSON *json = cJSON_ParseWithOpts(body, NULL, true);
    bool valid = assistant_settings_load(settings) == ESP_OK && cJSON_IsObject(json) &&
                 read_profile(json, &settings->client.profile);
    if (valid) {
        cJSON *tts = cJSON_GetObjectItemCaseSensitive(json, "tts_enabled");
        if (cJSON_IsBool(tts)) settings->client.tts_enabled = cJSON_IsTrue(tts);
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(json, "volume");
        if (cJSON_IsNumber(vol) && vol->valueint >= 0 && vol->valueint <= 100) settings->client.volume = (uint8_t)vol->valueint;
        cJSON *prompt = cJSON_GetObjectItemCaseSensitive(json, "system_prompt");
        if (cJSON_IsString(prompt)) {
            read_field(json, "system_prompt", settings->client.system_prompt, sizeof(settings->client.system_prompt));
        }
    }
    cJSON_Delete(json);
    memset(body, 0, req->content_len);
    free(body);
    esp_err_t err = valid ? assistant_settings_save(settings) : ESP_ERR_INVALID_ARG;
    memset(settings, 0, sizeof(*settings));
    free(settings);
    if (err == ESP_ERR_INVALID_ARG)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Save Wi-Fi first and check the assistant profile");
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Profile could not be saved");
    s_saved = true;
    err = json_response(req, "{\"saved\":true}");
    if (s_on_saved) s_on_saved();
    return err;
}

static esp_err_t wallpaper_status(httpd_req_t *req)
{
    if (!authorized(req)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    return json_response(req, assistant_wallpaper_available() ?
                         "{\"saved\":true}" : "{\"saved\":false}");
}

static esp_err_t wallpaper_upload(httpd_req_t *req)
{
    if (!authorized(req))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    if (s_saved) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Already saved; restarting");
    if (req->content_len <= 0 || (size_t)req->content_len != ASSISTANT_WALLPAPER_BYTES)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wallpaper must be 240x320 RGB565");
    esp_err_t err = assistant_wallpaper_begin();
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wallpaper storage unavailable");
    uint8_t buffer[1024];
    size_t received = 0;
    while (received < ASSISTANT_WALLPAPER_BYTES) {
        size_t remaining = ASSISTANT_WALLPAPER_BYTES - received;
        size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        int count = httpd_req_recv(req, (char *)buffer, wanted);
        if (count <= 0) {
            assistant_wallpaper_abort();
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Wallpaper upload timed out");
        }
        err = assistant_wallpaper_write(received, buffer, (size_t)count);
        if (err != ESP_OK) {
            assistant_wallpaper_abort();
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wallpaper could not be written");
        }
        received += (size_t)count;
    }
    err = assistant_wallpaper_commit();
    if (err != ESP_OK) {
        assistant_wallpaper_abort();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wallpaper could not be committed");
    }
    s_saved = true;
    err = json_response(req, "{\"saved\":true}");
    if (s_on_saved) s_on_saved();
    return err;
}

static esp_err_t wallpaper_clear(httpd_req_t *req)
{
    if (!authorized(req))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    if (s_saved) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Already saved; restarting");
    if (assistant_wallpaper_clear() != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wallpaper could not be cleared");
    s_saved = true;
    esp_err_t err = json_response(req, "{\"saved\":false}");
    if (s_on_saved) s_on_saved();
    return err;
}

static esp_err_t configure(httpd_req_t *req)
{
    if (!authorized(req))
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid setup session");
    if (s_saved) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Already saved; restarting");
    if (req->content_len <= 0 || req->content_len > 6144)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request size");
    char *body = calloc(1, req->content_len + 1);
    assistant_settings_t *settings = calloc(1, sizeof(*settings));
    if (!body || !settings) {
        free(body); free(settings);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Low memory");
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n <= 0) {
            free(body); free(settings);
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload timed out");
        }
        received += n;
    }
    cJSON *json = cJSON_ParseWithOpts(body, NULL, true);
    bool saved_wifi = assistant_settings_load(settings) == ESP_OK;
    if (!saved_wifi) assistant_settings_defaults(settings);
    bool wifi_only = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "wifi_only"));
    bool use_saved = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "use_saved_wifi"));
    if (!wifi_only) {
        assistant_backup_wifi_t backup = settings->backup;
        char ssid[sizeof(settings->ssid)], password[sizeof(settings->wifi_password)];
        memcpy(ssid, settings->ssid, sizeof(ssid));
        memcpy(password, settings->wifi_password, sizeof(password));
        assistant_settings_defaults(settings);
        settings->backup = backup;
        memset(&backup, 0, sizeof(backup));
        memcpy(settings->ssid, ssid, sizeof(ssid));
        memcpy(settings->wifi_password, password, sizeof(password));
        memset(password, 0, sizeof(password));
    }
    bool valid = cJSON_IsObject(json);
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(json, "mode");
    if (!wifi_only && (!cJSON_IsString(mode) ||
        (strcmp(mode->valuestring, "gateway") && strcmp(mode->valuestring, "direct")))) valid = false;
    else if (!wifi_only) settings->client.mode = strcmp(mode->valuestring, "direct") == 0 ? ASSISTANT_MODE_DIRECT : ASSISTANT_MODE_GATEWAY;
#define FIELD(name, field) valid = read_field(json, name, settings->field, sizeof(settings->field)) && valid
    if (use_saved) valid = valid && saved_wifi;
    else { FIELD("ssid", ssid); FIELD("wifi_password", wifi_password); }
    valid = read_backup_wifi(json, settings) && valid;
    if (!wifi_only && settings->client.mode == ASSISTANT_MODE_GATEWAY) {
        FIELD("gateway_url", client.gateway_url); FIELD("gateway_token", client.gateway_token);
    } else if (!wifi_only) {
        FIELD("tencent_secret_id", client.tencent_secret_id);
        FIELD("asr_url", client.asr_url); FIELD("asr_model", client.asr_model); FIELD("asr_key", client.asr_key);
        FIELD("deepseek_url", client.deepseek_url); FIELD("deepseek_model", client.deepseek_model); FIELD("deepseek_key", client.deepseek_key);
    }
    if (!wifi_only) {
        valid = read_profile(json, &settings->client.profile) && valid;
        cJSON *tts = cJSON_GetObjectItemCaseSensitive(json, "tts_enabled");
        if (cJSON_IsBool(tts)) settings->client.tts_enabled = cJSON_IsTrue(tts);
        cJSON *prompt = cJSON_GetObjectItemCaseSensitive(json, "system_prompt");
        if (cJSON_IsString(prompt)) {
            read_field(json, "system_prompt", settings->client.system_prompt, sizeof(settings->client.system_prompt));
        }
    } else {
        if (!assistant_profile_valid(&settings->client.profile)) {
            assistant_profile_defaults(&settings->client.profile);
        }
    }
#undef FIELD
    valid = valid && assistant_wifi_valid(settings) && (wifi_only || assistant_settings_valid(settings));
    cJSON_Delete(json);
    memset(body, 0, req->content_len);
    free(body);
    if (!valid) {
        memset(settings, 0, sizeof(*settings)); free(settings);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Check profile, Wi-Fi, URLs, models and keys; cloud URLs require HTTPS; gateway token requires 32+ characters");
    }
    esp_err_t err = assistant_settings_save(settings);
    memset(settings, 0, sizeof(*settings)); free(settings);
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Settings could not be saved");
    s_saved = true;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_sendstr(req, "{\"saved\":true}");
    if (s_on_saved) s_on_saved();
    return err;
}

esp_err_t assistant_setup_start(char ssid[33], char password[17], void (*on_saved)(void))
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    if (err != ESP_OK) return err;
    snprintf(ssid, 33, "Passport-%02X%02X%02X", mac[3], mac[4], mac[5]);
    strcpy(password, "qwerty1234@");
    for (size_t i = 0; i < 4; ++i) snprintf(s_csrf + i * 8, 9, "%08lx", (unsigned long)esp_random());
    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    if (!netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK) { esp_netif_destroy_default_wifi(netif); return err; }
    wifi_config_t config = {0};
    memcpy(config.ap.ssid, ssid, strlen(ssid));
    config.ap.ssid_len = strlen(ssid);
    strcpy((char *)config.ap.password, password);
    config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    config.ap.max_connection = 1;
    config.ap.channel = 1;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_AP, &config);
    if (err == ESP_OK) err = esp_wifi_start();
    httpd_handle_t server = NULL;
    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 6144;
    http.max_open_sockets = 2;
    http.max_uri_handlers = 8;
    http.lru_purge_enable = true;
    http.recv_wait_timeout = 20;
    if (err == ESP_OK) err = httpd_start(&server, &http);
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page};
    httpd_uri_t save = {.uri = "/configure", .method = HTTP_POST, .handler = configure};
    httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_saved};
    httpd_uri_t profile = {.uri = "/profile", .method = HTTP_GET, .handler = profile_status};
    httpd_uri_t profile_post = {.uri = "/profile", .method = HTTP_POST, .handler = profile_save};
    httpd_uri_t wallpaper_get = {.uri = "/wallpaper", .method = HTTP_GET, .handler = wallpaper_status};
    httpd_uri_t wallpaper_post = {.uri = "/wallpaper", .method = HTTP_POST, .handler = wallpaper_upload};
    httpd_uri_t wallpaper_delete = {.uri = "/wallpaper", .method = HTTP_DELETE, .handler = wallpaper_clear};
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &root);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &save);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &wifi);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &profile);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &profile_post);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &wallpaper_get);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &wallpaper_post);
    if (err == ESP_OK) err = httpd_register_uri_handler(server, &wallpaper_delete);
    if (err != ESP_OK) {
        if (server) httpd_stop(server);
        esp_wifi_stop(); esp_wifi_deinit(); esp_netif_destroy_default_wifi(netif);
        return err;
    }
    s_on_saved = on_saved;
    return ESP_OK;
}
