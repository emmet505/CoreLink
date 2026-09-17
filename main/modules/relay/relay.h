#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include "hal/gpio_types.h"
#include "stdbool.h"
#include "stdint.h"

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
  gpio_num_t gpio;
} relay_state_t;

esp_err_t relay_init(void);
esp_err_t relay_set_state(uint8_t relay_num, bool on);

relay_state_t* relay_get_state(uint8_t relay_num);
esp_err_t relay_handler_set(httpd_req_t* req);  // just turing on/off the relay
esp_err_t relay_handler_schedule(httpd_req_t* req);
esp_err_t relay_get_state_handler(httpd_req_t* req);
void relay_emergency_stop(uint8_t relay_num);


static void relay_schedule_task(void* arg);

static void relay_nvs_save(uint8_t relay_num);
static void relay_nvs_load(void);