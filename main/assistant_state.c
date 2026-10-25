#include "assistant_state.h"

voice_action_t voice_handle_input(voice_state_t *state, voice_input_t input)
{
    if (!state || *state == VOICE_SETUP || *state == VOICE_BUSY) return VOICE_NONE;
    if (*state == VOICE_RECORDING) {
        if (input != VOICE_OK) return VOICE_NONE;
        *state = VOICE_BUSY;
        return VOICE_STOP;
    }
    if (*state == VOICE_CONFIRM_CLEAR) {
        if (input == VOICE_OK) { *state = VOICE_BUSY; return VOICE_CLEAR; }
        if (input == VOICE_UP || input == VOICE_DOWN || input == VOICE_SETTINGS) {
            *state = VOICE_IDLE;
            return VOICE_CANCEL_CLEAR;
        }
        return VOICE_NONE;
    }
    if (input == VOICE_OK) { *state = VOICE_BUSY; return VOICE_START; }
    if (input == VOICE_UP) return VOICE_SCROLL_UP;
    if (input == VOICE_DOWN) return VOICE_SCROLL_DOWN;
    if (input == VOICE_FORGET) { *state = VOICE_CONFIRM_CLEAR; return VOICE_SHOW_CLEAR; }
    if (input == VOICE_SETTINGS) { *state = VOICE_SETUP; return VOICE_OPEN_SETTINGS; }
    return VOICE_NONE;
}
