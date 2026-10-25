#pragma once

#include "assistant_client.h"

typedef struct {
    char ssid[33];
    char password[65];
} assistant_backup_wifi_t;

typedef struct {
    uint32_t version;
    char ssid[33];
    char wifi_password[65];
    assistant_client_config_t client;
    assistant_backup_wifi_t backup; /* Appended: empty SSID disables failover. */
} assistant_settings_t;

void assistant_settings_defaults(assistant_settings_t *settings);
bool assistant_wifi_valid(const assistant_settings_t *settings);
bool assistant_backup_wifi_valid(const assistant_backup_wifi_t *backup);
/* Pure validation: may run on host. No secrets are included in error strings. */
bool assistant_settings_valid(const assistant_settings_t *settings);

