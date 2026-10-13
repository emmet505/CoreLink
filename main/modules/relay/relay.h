#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define RELAY_COUNT 4

typedef struct {
  uint8_t hour;
  uint8_t minute;
} relay_time_t;

typedef struct {
  relay_time_t start;
  relay_time_t stop;
  bool enabled;
} relay_schedule_t;

typedef struct {
  bool is_on;
  relay_schedule_t schedule;
} relay_state_t;

esp_err_t relay_init(void);
esp_err_t relay_set_state(uint8_t relay_num, bool on);  // relay_num: 1..4
void relay_emergency_stop_all(void);

/* HTTP handlers */
esp_err_t relay_handler_set(httpd_req_t* req);        // POST /api/relay
esp_err_t relay_handler_schedule(httpd_req_t* req);   // POST /api/relay/schedule
esp_err_t relay_get_state_handler(httpd_req_t* req);  // GET  /api/relay/state