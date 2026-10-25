#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fetch latest weather from wttr.in (lightweight, no API key needed).
 * Translates conditions to Chinese and extracts temperature, e.g. "多云 22℃". */
esp_err_t assistant_weather_fetch(char *out, size_t capacity);

/* Returns pointer to statically cached weather string. Thread-safe. */
const char *assistant_weather_get_cached(void);

/* Format current time string (HH:MM) according to China Standard Time (UTC+8).
 * Returns true if clock is synchronized, false otherwise. */
bool assistant_weather_get_time_str(char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
