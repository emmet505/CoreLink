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
 * SAFETY RULE: Group A and Group B must never be energized together.
 * It is enforced inside relay.c, in the single hardware layer that every
 * caller (API, scheduler, E-stop) goes through. Nothing outside relay.c
 * can touch the relay GPIOs.
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
  bool is_on;  // true only when BOTH relays of the group are ON
  relay_schedule_t schedule;
} relay_group_state_t;

/* All relays OFF, schedules loaded from NVS, scheduler task started. */
esp_err_t relay_init(void);

/*
 * Turn a group ON or OFF through the safety layer.
 * Turning a group ON while the other group is ON performs the safe sequence:
 * other group OFF -> verify -> dead time -> this group ON.
 * Blocks for up to ~0.7 s while such a transition runs.
 */
esp_err_t relay_group_set(relay_group_t group, bool on);

esp_err_t relay_group_get_state(relay_group_t group, relay_group_state_t* out);

/* Forces every relay OFF and disables the schedules (RAM only). */
void relay_emergency_stop_all(void);

/* HTTP handlers */
esp_err_t relay_handler_set(httpd_req_t* req);       // POST /api/relay
esp_err_t relay_handler_schedule(httpd_req_t* req);  // POST /api/relay/schedule
esp_err_t relay_get_state_handler(httpd_req_t* req); // GET  /api/relay/state