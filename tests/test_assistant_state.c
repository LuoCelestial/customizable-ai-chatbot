#include <assert.h>
#include <stdio.h>
#include "assistant_state.h"

int main(void)
{
    voice_state_t state = VOICE_IDLE;
    assert(voice_handle_input(&state, VOICE_FORGET) == VOICE_SHOW_CLEAR);
    assert(state == VOICE_CONFIRM_CLEAR);
    assert(voice_handle_input(&state, VOICE_DOWN) == VOICE_CANCEL_CLEAR);
    assert(state == VOICE_IDLE);
    assert(voice_handle_input(&state, VOICE_FORGET) == VOICE_SHOW_CLEAR);
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_CLEAR);
    assert(state == VOICE_BUSY);
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_NONE);
    state = VOICE_IDLE;
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_START);
    assert(voice_handle_input(&state, VOICE_SETTINGS) == VOICE_NONE);
    state = VOICE_RECORDING;
    assert(voice_handle_input(&state, VOICE_FORGET) == VOICE_NONE);
    assert(voice_handle_input(&state, VOICE_UP) == VOICE_NONE);
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_STOP);
    assert(state == VOICE_BUSY);
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_NONE);
    state = VOICE_IDLE;
    assert(voice_handle_input(&state, VOICE_SETTINGS) == VOICE_OPEN_SETTINGS);
    assert(state == VOICE_SETUP);
    assert(voice_handle_input(&state, VOICE_OK) == VOICE_NONE);
    puts("Assistant state tests: PASS");
}
