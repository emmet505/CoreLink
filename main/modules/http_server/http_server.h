#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

// Public API: start the HTTP server
esp_err_t http_server_start(void);
//httpd_handle_t http_server_get_handle(void);
