#include "assistant_weather.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "assistant_weather";
static char s_cached_weather[48] = "获取中...";

typedef struct {
    const char *en;
    const char *zh;
} weather_map_t;

static const weather_map_t s_map[] = {
    {"Thunderstorm", "雷雨"},
    {"Partly cloudy", "多云"},
    {"Cloudy", "阴"},
    {"Overcast", "阴"},
    {"Clear", "晴"},
    {"Sunny", "晴"},
    {"Light rain", "小雨"},
    {"Heavy rain", "大雨"},
    {"Shower", "阵雨"},
    {"Rain", "雨"},
    {"Snow", "雪"},
    {"Fog", "雾"},
    {"Mist", "薄雾"},
    {"Haze", "霾"},
};

static void translate_weather(const char *raw, char *out, size_t capacity)
{
    if (!raw || !out || capacity == 0) return;
    const char *cond = "多云";
    for (size_t i = 0; i < sizeof(s_map) / sizeof(s_map[0]); ++i) {
        if (strcasestr(raw, s_map[i].en)) {
            cond = s_map[i].zh;
            break;
        }
    }

    /* Extract temperature number */
    int temp = 20;
    bool found_temp = false;
    const char *p = raw;
    while (*p) {
        if ((*p == '+' || *p == '-' || isdigit((unsigned char)*p)) &&
            (isdigit((unsigned char)*(p + 1)) || isdigit((unsigned char)*p))) {
            int sign = 1;
            if (*p == '-') { sign = -1; p++; }
            else if (*p == '+') { p++; }
            if (isdigit((unsigned char)*p)) {
                temp = 0;
                while (isdigit((unsigned char)*p)) {
                    temp = temp * 10 + (*p - '0');
                    p++;
                }
                temp *= sign;
                found_temp = true;
                break;
            }
        }
        p++;
    }

    if (found_temp) {
        snprintf(out, capacity, "%s %d°C", cond, temp);
    } else {
        snprintf(out, capacity, "%s", cond);
    }
}

esp_err_t assistant_weather_fetch(char *out, size_t capacity)
{
    esp_http_client_config_t config = {
        .url = "https://wttr.in/?format=%C+%t",
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return ESP_FAIL;

    esp_http_client_set_header(client, "User-Agent", "curl/8.0.0");
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "Failed to open weather HTTP connection: %s", esp_err_to_name(err));
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    (void)content_len;
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        esp_http_client_cleanup(client);
        ESP_LOGW(TAG, "Weather returned status %d", status_code);
        return ESP_FAIL;
    }

    char raw[64] = {0};
    int r = esp_http_client_read(client, raw, sizeof(raw) - 1);
    esp_http_client_cleanup(client);
    if (r <= 0) return ESP_FAIL;
    raw[r] = '\0';

    char formatted[48];
    translate_weather(raw, formatted, sizeof(formatted));
    strncpy(s_cached_weather, formatted, sizeof(s_cached_weather) - 1);
    s_cached_weather[sizeof(s_cached_weather) - 1] = '\0';
    ESP_LOGI(TAG, "Weather updated: raw='%s', formatted='%s'", raw, s_cached_weather);

    if (out && capacity > 0) {
        strncpy(out, s_cached_weather, capacity - 1);
        out[capacity - 1] = '\0';
    }
    return ESP_OK;
}

const char *assistant_weather_get_cached(void)
{
    return s_cached_weather;
}

bool assistant_weather_get_time_str(char *out, size_t capacity)
{
    if (!out || capacity == 0) return false;
    time_t now = time(NULL);
    if (now < 1700000000) {
        snprintf(out, capacity, "--:--");
        return false;
    }
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    snprintf(out, capacity, "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
    return true;
}
