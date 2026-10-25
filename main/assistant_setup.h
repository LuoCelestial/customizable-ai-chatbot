#pragma once

#include "assistant_settings.h"

esp_err_t assistant_settings_load(assistant_settings_t *settings);
esp_err_t assistant_settings_save(const assistant_settings_t *settings);
/* Parses a local build-time preset atomically. Invalid input leaves settings unchanged. */
bool assistant_wifi_preset_apply(assistant_settings_t *settings, const char *json);
esp_err_t assistant_settings_request_setup(void);
bool assistant_settings_take_setup_request(void);
/* Network must be initialized; call only before station mode is started.
 * Starts a password-protected AP and local configuration HTTP server.
 * on_saved is a lightweight notification invoked after the response is sent.
 * Lifetime is until restart; all credentials persist in the app's NVS only. */
esp_err_t assistant_setup_start(char ssid[33], char password[17], void (*on_saved)(void));
