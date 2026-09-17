#pragma once
#include <time.h>

#include "esp_err.h"
#include "esp_http_server.h"

typedef struct {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
} local_time_t;

void time_print_current(void);

// Start SNTP using the configured upstream network connection.
esp_err_t time_start_ntp(void);

esp_err_t time_settings_get_handler(httpd_req_t* req);
esp_err_t time_settings_post_handler(httpd_req_t* req);

// HTTP handler — POST /api/time
esp_err_t time_sync_handler(httpd_req_t* req);

// Get current system time as string "HH:MM:SS"
void time_get_str(char* buf, size_t len);

// Check if time has been synced at least once
bool time_is_synced(void);
