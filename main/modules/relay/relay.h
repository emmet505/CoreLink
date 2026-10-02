#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

/*
 * Two independent loads, each switched by a phase relay and a neutral relay:
 *   Group A = relay 1 (phase) + relay 2 (neutral)
 *   Group B = relay 3 (phase) + relay 4 (neutral)
 *
 * Group A's output is reverse-wired: its load is ON when relays 1 and 2 are
 * de-energized. A request to turn B ON first turns A's output OFF, observes
 * dead time, then turns B ON. Turning A ON first turns B OFF.
 */
#define RELAY_GROUP_COUNT 2

typedef enum {
  RELAY_GROUP_A = 0,
  RELAY_GROUP_B = 1,
} relay_group_t;

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
  bool is_on;  // logical load output state, not relay-coil state
  relay_schedule_t schedule;
} relay_group_state_t;

/* Both load outputs OFF, schedules loaded from NVS, scheduler started. */
esp_err_t relay_init(void);

/*
 * Turn a group ON or OFF through the safety layer.
 * Turning B ON automatically turns A's output OFF first if needed.
 * Turning A ON first turns B OFF and observes dead time.
 */
esp_err_t relay_group_set(relay_group_t group, bool on);

esp_err_t relay_group_get_state(relay_group_t group, relay_group_state_t* out);

/* Turns both load outputs OFF and disables schedules (RAM only). */
void relay_emergency_stop_all(void);

/* HTTP handlers */
esp_err_t relay_handler_set(httpd_req_t* req);       // POST /api/relay
esp_err_t relay_handler_schedule(httpd_req_t* req);  // POST /api/relay/schedule
esp_err_t relay_get_state_handler(httpd_req_t* req);  // GET  /api/relay/state