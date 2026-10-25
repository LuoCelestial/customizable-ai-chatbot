#pragma once
#include <stdbool.h>

typedef enum { VOICE_IDLE, VOICE_BUSY, VOICE_RECORDING, VOICE_CONFIRM_CLEAR, VOICE_SETUP } voice_state_t;
typedef enum { VOICE_OK, VOICE_UP, VOICE_DOWN, VOICE_SETTINGS, VOICE_FORGET } voice_input_t;
typedef enum { VOICE_NONE, VOICE_START, VOICE_STOP, VOICE_SCROLL_UP, VOICE_SCROLL_DOWN,
               VOICE_SHOW_CLEAR, VOICE_CANCEL_CLEAR, VOICE_CLEAR, VOICE_OPEN_SETTINGS } voice_action_t;
voice_action_t voice_handle_input(voice_state_t *state, voice_input_t input);
