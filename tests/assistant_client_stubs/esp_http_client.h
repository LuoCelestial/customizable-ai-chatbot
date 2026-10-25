#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_DELETE } esp_http_client_method_t;
typedef struct {
    const char *url;
    esp_http_client_method_t method;
    int timeout_ms, buffer_size, buffer_size_tx;
    int (*crt_bundle_attach)(void *);
    bool disable_auto_redirect;
} esp_http_client_config_t;
typedef struct mock_http *esp_http_client_handle_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
