#include "assistant_wifi_policy.h"

bool assistant_wifi_failed(assistant_wifi_policy_t *state, bool backup_available)
{
    if (!state) return false;
    if (!backup_available) {
        state->failures = 0;
        state->using_backup = false;
        return false;
    }
    if (++state->failures < ASSISTANT_WIFI_FAILURE_LIMIT) return false;
    state->failures = 0;
    state->using_backup = !state->using_backup;
    return true;
}

void assistant_wifi_connected(assistant_wifi_policy_t *state)
{
    if (state) state->failures = 0;
}
