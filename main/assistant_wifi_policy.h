#pragma once

#include <stdbool.h>

#define ASSISTANT_WIFI_FAILURE_LIMIT 3

typedef struct {
    unsigned failures;
    bool using_backup;
} assistant_wifi_policy_t;

/* Owned by the application task. Apply a copy, then commit only after the
 * Wi-Fi driver accepts the target configuration. No networking or secrets. */
bool assistant_wifi_failed(assistant_wifi_policy_t *state, bool backup_available);
void assistant_wifi_connected(assistant_wifi_policy_t *state);
