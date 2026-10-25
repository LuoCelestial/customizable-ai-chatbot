#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "assistant_profile.h"

/* Call every function while holding bsp_lvgl_lock(). The application owns
 * scheduling, button dispatch and backlight control; this UI creates no tasks,
 * timers or input callbacks. Strings are copied before a function returns.
 * wallpaper_pixels may point to a persistent 240x320 little-endian RGB565 map. */
bool assistant_ui_init(const uint8_t *wallpaper_pixels, const assistant_profile_t *profile);
void assistant_ui_status(const char *state, const char *detail);
void assistant_ui_conversation(const char *transcript, const char *reply);
void assistant_ui_setup(const char *ssid, const char *password);
void assistant_ui_mode(bool direct_cloud);
void assistant_ui_battery(int percent);
void assistant_ui_time_weather(const char *time_str, const char *weather_str);
void assistant_ui_scroll(int direction);
void assistant_ui_recording(unsigned seconds, unsigned limit_seconds);
void assistant_ui_confirm_clear(bool active);

/* Call only after the application has stopped all callers of this UI.
 * A newly loaded blank screen replaces the UI before its objects are deleted. */
void assistant_ui_destroy(void);
