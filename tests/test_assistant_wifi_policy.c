#include <assert.h>
#include <stdio.h>
#include "assistant_wifi_policy.h"

int main(void)
{
    assistant_wifi_policy_t state = {0};
    for (unsigned i = 0; i < 100; ++i)
        assert(!assistant_wifi_failed(&state, false) && !state.using_backup);
    assert(!assistant_wifi_failed(&state, true));
    assert(!assistant_wifi_failed(&state, true));
    assert(assistant_wifi_failed(&state, true) && state.using_backup);
    assert(!assistant_wifi_failed(&state, true));
    assistant_wifi_connected(&state);
    assert(state.failures == 0 && state.using_backup);
    assert(!assistant_wifi_failed(&state, true));
    assert(!assistant_wifi_failed(&state, true));
    /* A rejected driver update must not commit the prospective target. */
    assistant_wifi_policy_t next = state;
    assert(assistant_wifi_failed(&next, true) && !next.using_backup);
    assert(state.using_backup && state.failures == 2);
    state = next;
    assert(!state.using_backup && state.failures == 0);
    assistant_wifi_connected(&state);
    assert(!assistant_wifi_failed(NULL, true));
    assistant_wifi_connected(NULL);
    puts("Primary/backup Wi-Fi failover policy: PASS");
}
