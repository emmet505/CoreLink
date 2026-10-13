#include "relay.h"

#include <stdio.h>
#include <stdlib.h>
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

#define RELAY_BODY_MAX 256
#define RELAY_SCHED_POLL_MS 5000

/* Active-low boards: level 0 = relay ON*/
static const gpio_num_t RELAY_GPIOS[RELAY_COUNT] = {GPIO_NUM_9, GPIO_NUM_3,
                                                    GPIO_NUM_8, GPIO_NUM_18};

static relay_state_t s_relays[RELAY_COUNT];
/* Last state the scheduler asked for: -1 = unknown, 0 = OFF, 1 = ON.
 * The scheduler only acts when this CHANGES, so a manual ON/OFF is not
 * overridden 5 seconds later. */
static int8_t s_last_want[RELAY_COUNT];
static SemaphoreHandle_t s_mutex = NULL;

/* ── Core helpers ───────────────────────────────────────────────────────────
 */

/* Caller must hold s_mutex (or be single-threaded, like relay_init). */
static void apply_locked(int i, bool on) {
  s_relays[i].is_on = on;
  gpio_set_level(RELAY_GPIOS[i], on ? 0 : 1);
}

static bool time_valid(const relay_time_t* t) {
  return t->hour <= 23 && t->minute <= 59;
}

static bool schedule_is_active(const relay_schedule_t* s, uint16_t now_min) {
  uint16_t start = s->start.hour * 60 + s->start.minute;
  uint16_t stop = s->stop.hour * 60 + s->stop.minute;

  if (start < stop) return now_min >= start && now_min < stop;
  if (start > stop) return now_min >= start || now_min < stop;  // past midnight
  return false;
}

/* ── NVS: one blob per relay (state + schedule) ─────────────────────────────
 */

static void relay_nvs_save(int i) {
  relay_state_t snap;  // copy under the lock, write flash outside it
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  snap = s_relays[i];
  xSemaphoreGive(s_mutex);

  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

  char key[16];
  snprintf(key, sizeof(key), "r%d", i + 1);
  if (nvs_set_blob(handle, key, &snap, sizeof(snap)) == ESP_OK) {
    nvs_commit(handle);
  } else {
    ESP_LOGW(TAG, "Relay %d: NVS save failed", i + 1);
  }
  nvs_close(handle);
}

static void relay_nvs_load(void) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

  for (int i = 0; i < RELAY_COUNT; i++) {
    char key[8];
    relay_state_t saved;
    size_t len = sizeof(saved);
    snprintf(key, sizeof(key), "r%d", i + 1);

    if (nvs_get_blob(handle, key, &saved, &len) != ESP_OK ||
        len != sizeof(saved)) {
      continue;  // nothing saved (or old format): stay OFF
    }
    if (!time_valid(&saved.schedule.start) ||
        !time_valid(&saved.schedule.stop)) {
      saved.schedule.enabled = false;
    }
    s_relays[i].schedule = saved.schedule;
    apply_locked(i, saved.is_on);
  }
  nvs_close(handle);
}

/* ── Scheduler ──────────────────────────────────────────────────────────────
 */

static void relay_schedule_task(void* arg) {
  (void)arg;
  while (1) {
    if (time_is_synced()) {
      time_t now = time(NULL);
      struct tm lt;
      localtime_r(&now, &lt);
      uint16_t minute = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);

      xSemaphoreTake(s_mutex, portMAX_DELAY);
      for (int i = 0; i < RELAY_COUNT; i++) {
        if (!s_relays[i].schedule.enabled) {
          s_last_want[i] = -1;
          continue;
        }
        int8_t want = schedule_is_active(&s_relays[i].schedule, minute);
        if (want == s_last_want[i]) continue;  // no transition, leave it alone

        s_last_want[i] = want;
        if (s_relays[i].is_on != (bool)want) {
          apply_locked(i, want);
          ESP_LOGI(TAG, "Relay %d -> %s (schedule)", i + 1,
                   want ? "ON" : "OFF");
        }
      }
      xSemaphoreGive(s_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(RELAY_SCHED_POLL_MS));
  }
}

/* ── Public API ─────────────────────────────────────────────────────────────
 */

esp_err_t relay_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }
  memset(s_relays, 0, sizeof(s_relays));
  memset(s_last_want, -1, sizeof(s_last_want));

  for (int i = 0; i < RELAY_COUNT; i++) {
    /* Set the OFF level BEFORE enabling the output, so the pin never
     * glitches low (= relay click) during configuration. */
    gpio_set_level(RELAY_GPIOS[i], 1);
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIOS[i]),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "gpio_config failed for relay %d", i + 1);
      return err;
    }
  }

  relay_nvs_load();
  xTaskCreate(relay_schedule_task, "relay_sched", 3072, NULL, 5, NULL);
  ESP_LOGI(TAG, "Relay module initialized");
  return ESP_OK;
}

esp_err_t relay_set_state(uint8_t relay_num, bool on) {
  if (relay_num < 1 || relay_num > RELAY_COUNT) return ESP_ERR_INVALID_ARG;

  int i = relay_num - 1;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  apply_locked(i, on);
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Relay %d -> %s", relay_num, on ? "ON" : "OFF");
  return ESP_OK;
}

void relay_emergency_stop_all(void) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (int i = 0; i < RELAY_COUNT; i++) {
    apply_locked(i, false);
    s_relays[i].schedule.enabled = false;  // RAM only; NVS is left untouched
    s_last_want[i] = -1;
  }
  xSemaphoreGive(s_mutex);
  ESP_LOGW(TAG, "Emergency stop: all relays OFF, schedules disabled");
}

/* ── HTTP helpers ───────────────────────────────────────────────────────────
 */

static esp_err_t send_error(httpd_req_t* req, const char* status,
                            const char* msg) {
  char body[96];
  snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", msg);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, body);
  return ESP_OK;
}

static esp_err_t send_ok(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}

/* Reads the whole body (httpd_req_recv may return it in pieces).
 * Returns NULL after sending an error response. */
static cJSON* read_json_body(httpd_req_t* req) {
  size_t len = req->content_len;
  if (len == 0 || len > RELAY_BODY_MAX) {
    send_error(req, "400 Bad Request", "Invalid body size");
    return NULL;
  }

  char buf[RELAY_BODY_MAX + 1];
  size_t got = 0;
  int timeouts = 0;
  while (got < len) {  // got : the number of bytes received so far
    int rec = httpd_req_recv(req, buf + got, len - got);
    if (rec == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
    if (rec <= 0) {
      send_error(req, "400 Bad Request", "Receive failed");
      return NULL;
    }
    got += (size_t)rec;
  }
  buf[got] = '\0';

  cJSON* json = cJSON_Parse(buf);
  if (json == NULL) send_error(req, "400 Bad Request", "Invalid JSON");
  return json;
}

/* "relay": 1..4  ->  index 0..3 */
static bool parse_relay(const cJSON* j, int* index) {
  if (!cJSON_IsNumber(j)) return false;
  int n = (int)j->valuedouble;
  if (n < 1 || n > RELAY_COUNT) return false;
  *index = n - 1;
  return true;
}

static bool parse_time(const cJSON* obj, relay_time_t* out) {
  const cJSON* h = cJSON_GetObjectItem(obj, "hour");
  const cJSON* m = cJSON_GetObjectItem(obj, "minute");
  if (!cJSON_IsNumber(h) || !cJSON_IsNumber(m)) return false;
  if (h->valuedouble < 0 || h->valuedouble > 23) return false;
  if (m->valuedouble < 0 || m->valuedouble > 59) return false;
  out->hour = (uint8_t)h->valuedouble;
  out->minute = (uint8_t)m->valuedouble;
  return true;
}

/* ── HTTP handlers ──────────────────────────────────────────────────────────
 */

esp_err_t relay_get_state_handler(httpd_req_t* req) {
  relay_state_t snap[RELAY_COUNT];
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  memcpy(snap, s_relays, sizeof(snap));
  xSemaphoreGive(s_mutex);

  /* cJSON_Add*ToObject() are NULL-safe, so one final check is enough. */
  cJSON* root = cJSON_CreateArray();
  for (int i = 0; i < RELAY_COUNT && root != NULL; i++) {
    cJSON* item = cJSON_CreateObject();
    if (item == NULL) {
      cJSON_Delete(root);
      root = NULL;
      break;
    }
    cJSON_AddItemToArray(root, item);

    cJSON_AddNumberToObject(item, "relay", i + 1);
    cJSON_AddBoolToObject(item, "is_on", snap[i].is_on);
    cJSON* sch = cJSON_AddObjectToObject(item, "schedule");
    cJSON_AddBoolToObject(sch, "enabled", snap[i].schedule.enabled);
    cJSON* st = cJSON_AddObjectToObject(sch, "start");
    cJSON_AddNumberToObject(st, "hour", snap[i].schedule.start.hour);
    cJSON_AddNumberToObject(st, "minute", snap[i].schedule.start.minute);
    cJSON* sp = cJSON_AddObjectToObject(sch, "stop");
    cJSON_AddNumberToObject(sp, "hour", snap[i].schedule.stop.hour);
    cJSON_AddNumberToObject(sp, "minute", snap[i].schedule.stop.minute);
  }

  char* json = root ? cJSON_PrintUnformatted(root) : NULL;
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

/* POST /api/relay  {"relay":1..4,"state":true|false} */
esp_err_t relay_handler_set(httpd_req_t* req) {
  cJSON* json = read_json_body(req);
  if (json == NULL) return ESP_OK;

  int i;  // i: relay index (0..3)
  const cJSON* j_state = cJSON_GetObjectItem(json, "state");
  bool valid = parse_relay(cJSON_GetObjectItem(json, "relay"), &i) &&
               cJSON_IsBool(j_state);
  bool state = cJSON_IsTrue(j_state);
  cJSON_Delete(json);
  if (!valid)
    return send_error(req, "400 Bad Request", "Invalid relay or state");

  relay_set_state(i + 1, state);
  relay_nvs_save(i);
  return send_ok(req);
}

/* POST /api/relay/schedule
 *   {"relay":n,"enabled":false}
 *   {"relay":n,"enabled":true,"start":{hour,minute},"stop":{hour,minute}} */
esp_err_t relay_handler_schedule(httpd_req_t* req) {
  cJSON* json = read_json_body(req);
  if (json == NULL) return ESP_OK;

  int i;
  const cJSON* j_enabled = cJSON_GetObjectItem(json, "enabled");
  if (!parse_relay(cJSON_GetObjectItem(json, "relay"), &i) ||
      !cJSON_IsBool(j_enabled)) {
    cJSON_Delete(json);
    return send_error(req, "400 Bad Request", "Invalid relay or enabled");
  }

  if (!cJSON_IsTrue(j_enabled)) {
    cJSON_Delete(json);
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_relays[i].schedule.enabled = false;
    s_last_want[i] = -1;
    apply_locked(i, false);  // disabling the schedule also switches it OFF
    xSemaphoreGive(s_mutex);
    relay_nvs_save(i);
    ESP_LOGI(TAG, "Relay %d schedule disabled", i + 1);
    return send_ok(req);
  }

  relay_schedule_t sch = {.enabled = true};
  bool valid = parse_time(cJSON_GetObjectItem(json, "start"), &sch.start) &&
               parse_time(cJSON_GetObjectItem(json, "stop"), &sch.stop);
  cJSON_Delete(json);
  if (!valid)
    return send_error(req, "400 Bad Request", "Invalid schedule time");
  if (sch.start.hour == sch.stop.hour && sch.start.minute == sch.stop.minute) {
    return send_error(req, "400 Bad Request", "Start and stop must differ");
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_relays[i].schedule = sch;
  s_last_want[i] = -1;  // force the scheduler to apply it on its next tick
  xSemaphoreGive(s_mutex);
  relay_nvs_save(i);

  ESP_LOGI(TAG, "Relay %d schedule: %02d:%02d -> %02d:%02d", i + 1,
           sch.start.hour, sch.start.minute, sch.stop.hour, sch.stop.minute);
  return send_ok(req);
}