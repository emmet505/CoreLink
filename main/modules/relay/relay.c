#include "relay.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "time_handler.h"

static const char* TAG = "RELAY";
static const char* NVS_NAMESPACE = "relay_cfg";

#define RELAY_COUNT 4
#define RELAY_BODY_MAX 256
#define RELAY_GAP_MS 50          /* phase/neutral spacing inside one group */
#define RELAY_DEAD_TIME_MS 500   /* both groups OFF before the other turns ON */
#define RELAY_SCHED_POLL_MS 5000

/* Relay i (0..3) -> GPIO. Boards are active-low: level 0 = relay ON. */
static const gpio_num_t RELAY_GPIOS[RELAY_COUNT] = {GPIO_NUM_15, GPIO_NUM_16,
                                                    GPIO_NUM_17, GPIO_NUM_18};

/* Group g: relay 2g = phase, relay 2g+1 = neutral. */
static inline int phase_relay(int g) { return g * 2; }
static inline int neutral_relay(int g) { return g * 2 + 1; }
static inline char group_name(int g) { return g == 0 ? 'A' : 'B'; }

/*
 * Two locks, two jobs:
 *  s_switch_mutex : serializes every hardware change. It is held for the whole
 *                   transition (including the dead time), so two requests can
 *                   never interleave their GPIO writes.
 *  s_data_mutex   : protects s_groups for short reads/writes only, so GET
 *                   /api/relay/state never waits for a transition.
 * Lock order is always switch -> data, never the reverse.
 */
static SemaphoreHandle_t s_switch_mutex = NULL;
static SemaphoreHandle_t s_data_mutex = NULL;
static relay_group_state_t s_groups[RELAY_GROUP_COUNT];
static atomic_bool s_abort = false;     /* set by E-stop to cancel transitions */
static atomic_bool s_conflict = false;  /* both schedules want to be ON */

static void relay_schedule_task(void* arg);

/* ══════════════════════════════════════════════════════════════════════════
 *  Hardware layer — the ONLY code that writes relay GPIOs
 * ══════════════════════════════════════════════════════════════════════════ */

static void hw_write(int relay, bool on) {
  gpio_set_level(RELAY_GPIOS[relay], on ? 0 : 1);
}

/* Reads the pin back (pins are configured INPUT_OUTPUT). This proves what we
 * drive, not that the contacts opened; a welded contact cannot be seen here. */
static bool hw_is_on(int relay) {
  return gpio_get_level(RELAY_GPIOS[relay]) == 0;
}

static bool group_hw_any_on(int g) {
  return hw_is_on(phase_relay(g)) || hw_is_on(neutral_relay(g));
}

static bool group_hw_all_on(int g) {
  return hw_is_on(phase_relay(g)) && hw_is_on(neutral_relay(g));
}

static void delay_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void data_set_on(int g, bool on) {
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_groups[g].is_on = on;
  xSemaphoreGive(s_data_mutex);
}

/* Turn a group OFF: phase first, then neutral (never leave a live phase
 * behind a floating neutral). */
static void group_hw_off(int g) {
  hw_write(phase_relay(g), false);
  delay_ms(RELAY_GAP_MS);
  hw_write(neutral_relay(g), false);
}

/* Turn a group ON: neutral first, then phase. Refuses if the other group is
 * not completely OFF (last line of defence, checked on the real pins). */
static esp_err_t group_hw_on(int g) {
  const int other = 1 - g;
  if (group_hw_any_on(other)) {
    ESP_LOGE(TAG, "INTERLOCK: group %c still energized, refusing group %c",
             group_name(other), group_name(g));
    return ESP_ERR_INVALID_STATE;
  }
  hw_write(neutral_relay(g), true);
  delay_ms(RELAY_GAP_MS);
  if (s_abort || group_hw_any_on(other)) {
    hw_write(neutral_relay(g), false);
    return ESP_ERR_INVALID_STATE;
  }
  hw_write(phase_relay(g), true);
  return ESP_OK;
}

/* Caller must hold s_switch_mutex. */
static esp_err_t group_apply_locked(int g, bool on) {
  const int other = 1 - g;

  if (s_abort) return ESP_ERR_INVALID_STATE;

  if (!on) {
    if (group_hw_any_on(g)) group_hw_off(g);
    data_set_on(g, false);
    ESP_LOGI(TAG, "Group %c OFF", group_name(g));
    return ESP_OK;
  }

  if (group_hw_all_on(g) && !group_hw_any_on(other)) {
    data_set_on(g, true);
    return ESP_OK;
  }

  bool switched = false;
  if (group_hw_any_on(other)) {
    ESP_LOGW(TAG, "Switching: group %c OFF before group %c ON",
             group_name(other), group_name(g));
    group_hw_off(other);
    data_set_on(other, false);
    switched = true;
  }

  /* Verify the other group really is OFF before going on. */
  if (group_hw_any_on(other)) {
    ESP_LOGE(TAG, "Group %c did not turn OFF, aborting switch",
             group_name(other));
    return ESP_FAIL;
  }

  if (switched) delay_ms(RELAY_DEAD_TIME_MS);
  if (s_abort) return ESP_ERR_INVALID_STATE;

  esp_err_t err = group_hw_on(g);
  if (err != ESP_OK) {
    if (group_hw_any_on(g)) group_hw_off(g);
    data_set_on(g, false);
    return err;
  }
  data_set_on(g, true);
  ESP_LOGI(TAG, "Group %c ON", group_name(g));
  return ESP_OK;
}

esp_err_t relay_group_set(relay_group_t group, bool on) {
  if ((unsigned)group >= RELAY_GROUP_COUNT) return ESP_ERR_INVALID_ARG;
  if (s_switch_mutex == NULL) return ESP_ERR_INVALID_STATE;

  xSemaphoreTake(s_switch_mutex, portMAX_DELAY);
  esp_err_t err = group_apply_locked((int)group, on);
  xSemaphoreGive(s_switch_mutex);
  return err;
}

void relay_emergency_stop_all(void) {
  if (s_switch_mutex == NULL) return;

  /* 1. Tell any running transition to give up, and cut power right now.
   *    Turning OFF can never violate the interlock, so no lock is needed. */
  s_abort = true;
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) hw_write(phase_relay(g), false);
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) hw_write(neutral_relay(g), false);

  /* 2. Wait for the transition to leave, then cut again (covers a write that
   *    slipped in between step 1 and the transition noticing the flag). */
  xSemaphoreTake(s_switch_mutex, portMAX_DELAY);
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) hw_write(phase_relay(g), false);
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) hw_write(neutral_relay(g), false);

  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    s_groups[g].is_on = false;
    s_groups[g].schedule.enabled = false;  // RAM only; NVS keeps the schedule
  }
  xSemaphoreGive(s_data_mutex);

  s_abort = false;
  xSemaphoreGive(s_switch_mutex);
  ESP_LOGW(TAG, "Emergency stop: all relays OFF, schedules disabled");
}

esp_err_t relay_group_get_state(relay_group_t group, relay_group_state_t* out) {
  if ((unsigned)group >= RELAY_GROUP_COUNT || out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  *out = s_groups[group];
  xSemaphoreGive(s_data_mutex);
  return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  Schedule logic
 * ══════════════════════════════════════════════════════════════════════════ */

static bool relay_schedule_is_active(const relay_schedule_t* schedule,
                                     uint16_t current_minute) {
  uint16_t start = schedule->start.hour * 60 + schedule->start.minute;
  uint16_t stop = schedule->stop.hour * 60 + schedule->stop.minute;

  if (start < stop) {
    return current_minute >= start && current_minute < stop;
  }
  if (start > stop) {
    return current_minute >= start || current_minute < stop;
  }
  return false;
}

static bool schedules_overlap(const relay_schedule_t* a,
                              const relay_schedule_t* b) {
  for (uint16_t m = 0; m < 24 * 60; m++) {
    if (relay_schedule_is_active(a, m) && relay_schedule_is_active(b, m)) {
      return true;
    }
  }
  return false;
}

static void relay_schedule_tick(void) {
  time_t now = time(NULL);
  struct tm lt;
  localtime_r(&now, &lt);
  uint16_t minute = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);

  relay_group_state_t snap[RELAY_GROUP_COUNT];
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  memcpy(snap, s_groups, sizeof(snap));
  xSemaphoreGive(s_data_mutex);

  bool scheduled[RELAY_GROUP_COUNT];
  bool want_on[RELAY_GROUP_COUNT];
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    scheduled[g] = snap[g].schedule.enabled;
    want_on[g] = scheduled[g] && relay_schedule_is_active(&snap[g].schedule, minute);
  }

  /* 1. Groups whose window is over go OFF first. */
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    if (scheduled[g] && !want_on[g] && snap[g].is_on) {
      ESP_LOGI(TAG, "Schedule: group %c window ended", group_name(g));
      relay_group_set((relay_group_t)g, false);
    }
  }

  /* 2. Both windows active at once: never turn a second group on. */
  bool conflict = want_on[0] && want_on[1];
  if (conflict != s_conflict) {
    s_conflict = conflict;
    if (conflict) {
      ESP_LOGE(TAG, "Schedule CONFLICT: A and B both due ON at %02d:%02d, "
                    "not switching anything on",
               lt.tm_hour, lt.tm_min);
    } else {
      ESP_LOGI(TAG, "Schedule conflict cleared");
    }
  }
  if (conflict) return;

  /* 3. Groups whose window is open go ON (safety layer handles the rest). */
  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    if (want_on[g] && !snap[g].is_on) {
      ESP_LOGI(TAG, "Schedule: group %c window started", group_name(g));
      esp_err_t err = relay_group_set((relay_group_t)g, true);
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Schedule: group %c ON refused (%s)", group_name(g),
                 esp_err_to_name(err));
      }
    }
  }
}

static void relay_schedule_task(void* arg) {
  (void)arg;
  while (1) {
    if (time_is_synced()) relay_schedule_tick();
    vTaskDelay(pdMS_TO_TICKS(RELAY_SCHED_POLL_MS));
  }
}

/* ══════════════════════════════════════════════════════════════════════════
 *  NVS — schedules only. Relay ON/OFF state is deliberately NOT restored:
 *  every boot starts with all relays OFF and the scheduler decides.
 * ══════════════════════════════════════════════════════════════════════════ */

static const char* NVS_FIELDS[5] = {"en", "sh", "sm", "eh", "em"};

static void nvs_key(char* key, size_t n, int g, const char* field) {
  snprintf(key, n, "g%d_%s", g + 1, field);
}

static esp_err_t relay_nvs_save_group(int g) {
  relay_schedule_t sch;
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  sch = s_groups[g].schedule;
  xSemaphoreGive(s_data_mutex);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  char key[16];
  if (!sch.enabled) {
    for (int i = 0; i < 5; i++) {
      nvs_key(key, sizeof(key), g, NVS_FIELDS[i]);
      nvs_erase_key(handle, key);  // NOT_FOUND is fine
    }
  } else {
    const uint8_t vals[5] = {1, sch.start.hour, sch.start.minute,
                             sch.stop.hour, sch.stop.minute};
    for (int i = 0; i < 5; i++) {
      nvs_key(key, sizeof(key), g, NVS_FIELDS[i]);
      err = nvs_set_u8(handle, key, vals[i]);
      if (err != ESP_OK) {
        nvs_close(handle);
        return err;
      }
    }
  }
  err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

/* Anything missing or out of range leaves that group's schedule DISABLED. */
static void relay_nvs_load(void) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    char key[16];
    uint8_t v[5] = {0};

    nvs_key(key, sizeof(key), g, NVS_FIELDS[0]);
    if (nvs_get_u8(handle, key, &v[0]) != ESP_OK || v[0] != 1) continue;

    bool ok = true;
    for (int i = 1; i < 5; i++) {
      nvs_key(key, sizeof(key), g, NVS_FIELDS[i]);
      if (nvs_get_u8(handle, key, &v[i]) != ESP_OK) ok = false;
    }
    if (ok && (v[1] > 23 || v[3] > 23 || v[2] > 59 || v[4] > 59)) ok = false;

    if (!ok) {
      ESP_LOGW(TAG, "Group %c: stored schedule invalid, left disabled",
               group_name(g));
      continue;
    }
    s_groups[g].schedule.enabled = true;
    s_groups[g].schedule.start.hour = v[1];
    s_groups[g].schedule.start.minute = v[2];
    s_groups[g].schedule.stop.hour = v[3];
    s_groups[g].schedule.stop.minute = v[4];
  }
  nvs_close(handle);

  if (s_groups[0].schedule.enabled && s_groups[1].schedule.enabled &&
      schedules_overlap(&s_groups[0].schedule, &s_groups[1].schedule)) {
    ESP_LOGW(TAG, "Stored schedules of A and B overlap; the scheduler will "
                  "refuse to turn both on");
  }
}

esp_err_t relay_init(void) {
  s_switch_mutex = xSemaphoreCreateMutex();
  s_data_mutex = xSemaphoreCreateMutex();
  if (s_switch_mutex == NULL || s_data_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutexes");
    return ESP_FAIL;
  }
  memset(s_groups, 0, sizeof(s_groups));

  /* Boot state: every relay OFF. The level is written before the pin becomes
   * an output so it never drives a stale level. */
  for (int i = 0; i < RELAY_COUNT; i++) {
    gpio_set_level(RELAY_GPIOS[i], 1);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIOS[i]),
        .mode = GPIO_MODE_INPUT_OUTPUT,  // INPUT_OUTPUT so the level can be read back
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "gpio_config failed for relay %d", i + 1);
      return err;
    }
    gpio_set_level(RELAY_GPIOS[i], 1);
  }

  relay_nvs_load();
  xTaskCreate(relay_schedule_task, "relay_sched", 3072, NULL, 5, NULL);
  ESP_LOGI(TAG, "Relay module initialized, all relays OFF");
  return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════════
 *  HTTP handlers
 * ══════════════════════════════════════════════════════════════════════════ */

static esp_err_t send_json_error(httpd_req_t* req, const char* status,
                                 const char* msg) {
  char body[96];
  snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", msg);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, body);
  return ESP_OK;
}

/* Returns NULL after sending an error response. */
static cJSON* read_json_body(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > RELAY_BODY_MAX) {
    send_json_error(req, "400 Bad Request", "Invalid body size");
    return NULL;
  }
  char buf[RELAY_BODY_MAX + 1];
  size_t total = 0;
  while (total < req->content_len) {
    int r = httpd_req_recv(req, buf + total, req->content_len - total);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      send_json_error(req, "400 Bad Request", "Receive failed");
      return NULL;
    }
    total += (size_t)r;
  }
  buf[total] = '\0';

  cJSON* json = cJSON_Parse(buf);
  if (json == NULL) send_json_error(req, "400 Bad Request", "Invalid JSON");
  return json;
}

static bool parse_group(const cJSON* j, int* out) {
  if (!cJSON_IsNumber(j)) return false;
  int n = (int)cJSON_GetNumberValue((cJSON*)j);
  if (n < 1 || n > RELAY_GROUP_COUNT) return false;
  *out = n - 1;
  return true;
}

static bool parse_time(const cJSON* obj, relay_time_t* out) {
  if (!cJSON_IsObject(obj)) return false;
  cJSON* h = cJSON_GetObjectItem(obj, "hour");
  cJSON* m = cJSON_GetObjectItem(obj, "minute");
  if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) return false;
  double hv = cJSON_GetNumberValue(h);
  double mv = cJSON_GetNumberValue(m);
  if (hv < 0 || hv > 23 || mv < 0 || mv > 59) return false;
  out->hour = (uint8_t)hv;
  out->minute = (uint8_t)mv;
  return true;
}

esp_err_t relay_get_state_handler(httpd_req_t* req) {
  relay_group_state_t snap[RELAY_GROUP_COUNT];
  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  memcpy(snap, s_groups, sizeof(snap));
  xSemaphoreGive(s_data_mutex);
  const bool conflict = s_conflict;

  cJSON* root = cJSON_CreateArray();
  if (root == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  for (int g = 0; g < RELAY_GROUP_COUNT; g++) {
    cJSON* item = cJSON_CreateObject();
    if (item == NULL) {
      cJSON_Delete(root);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    cJSON_AddItemToArray(root, item);

    cJSON_AddNumberToObject(item, "group", g + 1);
    cJSON* relays = cJSON_AddArrayToObject(item, "relays");
    if (relays != NULL) {
      cJSON_AddItemToArray(relays, cJSON_CreateNumber(phase_relay(g) + 1));
      cJSON_AddItemToArray(relays, cJSON_CreateNumber(neutral_relay(g) + 1));
    }
    cJSON_AddBoolToObject(item, "is_on", snap[g].is_on);
    cJSON_AddBoolToObject(item, "phase_on", hw_is_on(phase_relay(g)));
    cJSON_AddBoolToObject(item, "neutral_on", hw_is_on(neutral_relay(g)));
    cJSON_AddBoolToObject(item, "conflict", conflict);

    cJSON* sch = cJSON_AddObjectToObject(item, "schedule");
    cJSON_AddBoolToObject(sch, "enabled", snap[g].schedule.enabled);
    cJSON* st = cJSON_AddObjectToObject(sch, "start");
    cJSON_AddNumberToObject(st, "hour", snap[g].schedule.start.hour);
    cJSON_AddNumberToObject(st, "minute", snap[g].schedule.start.minute);
    cJSON* sp = cJSON_AddObjectToObject(sch, "stop");
    cJSON_AddNumberToObject(sp, "hour", snap[g].schedule.stop.hour);
    cJSON_AddNumberToObject(sp, "minute", snap[g].schedule.stop.minute);
  }

  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  return err;
}

/* POST /api/relay   {"group":1|2,"state":true|false} */
esp_err_t relay_handler_set(httpd_req_t* req) {
  cJSON* json = read_json_body(req);
  if (json == NULL) return ESP_OK;

  int g;
  cJSON* j_state = cJSON_GetObjectItem(json, "state");
  if (!parse_group(cJSON_GetObjectItem(json, "group"), &g) ||
      !cJSON_IsBool(j_state)) {
    cJSON_Delete(json);
    return send_json_error(req, "400 Bad Request", "Invalid group or state");
  }
  bool state = cJSON_IsTrue(j_state);
  cJSON_Delete(json);

  esp_err_t err = relay_group_set((relay_group_t)g, state);
  if (err != ESP_OK) {
    return send_json_error(req, "409 Conflict",
                           "Switch refused by safety layer");
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}

/* POST /api/relay/schedule
 *   {"group":1|2,"enabled":false}
 *   {"group":1|2,"enabled":true,"start":{hour,minute},"stop":{hour,minute}} */
esp_err_t relay_handler_schedule(httpd_req_t* req) {
  cJSON* json = read_json_body(req);
  if (json == NULL) return ESP_OK;

  int g;
  cJSON* j_enabled = cJSON_GetObjectItem(json, "enabled");
  if (!parse_group(cJSON_GetObjectItem(json, "group"), &g) ||
      !cJSON_IsBool(j_enabled)) {
    cJSON_Delete(json);
    return send_json_error(req, "400 Bad Request", "Invalid group or enabled");
  }
  const bool enabled = cJSON_IsTrue(j_enabled);

  if (!enabled) {
    cJSON_Delete(json);
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_groups[g].schedule.enabled = false;
    xSemaphoreGive(s_data_mutex);
    esp_err_t off = relay_group_set((relay_group_t)g, false);
    relay_nvs_save_group(g);
    if (off != ESP_OK) {
      return send_json_error(req, "409 Conflict", "Could not turn group off");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
  }

  relay_time_t start, stop;
  bool valid = parse_time(cJSON_GetObjectItem(json, "start"), &start) &&
               parse_time(cJSON_GetObjectItem(json, "stop"), &stop);
  cJSON_Delete(json);
  if (!valid) {
    return send_json_error(req, "400 Bad Request", "Invalid schedule time");
  }

  relay_schedule_t candidate = {.start = start, .stop = stop, .enabled = true};
  relay_group_state_t other;
  relay_group_get_state((relay_group_t)(1 - g), &other);
  if (other.schedule.enabled && schedules_overlap(&candidate, &other.schedule)) {
    return send_json_error(req, "409 Conflict",
                           "Schedule overlaps with the other group");
  }

  xSemaphoreTake(s_data_mutex, portMAX_DELAY);
  s_groups[g].schedule = candidate;
  xSemaphoreGive(s_data_mutex);

  if (relay_nvs_save_group(g) != ESP_OK) {
    ESP_LOGW(TAG, "Group %c schedule not saved to NVS", group_name(g));
  }
  ESP_LOGI(TAG, "Group %c schedule: %02d:%02d -> %02d:%02d", group_name(g),
           start.hour, start.minute, stop.hour, stop.minute);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}