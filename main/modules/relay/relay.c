#include "relay.h"

#include "cJSON.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "time_handler.h"

static const char* TAG = "RELAY";
static const char* NVS_NAMESPACE = "relay_cfg";

#define RELAY_COUNT 4

static const gpio_num_t RELAY_GPIOS[RELAY_COUNT] = {GPIO_NUM_15, GPIO_NUM_16,
                                                    GPIO_NUM_17, GPIO_NUM_18};

static relay_state_t s_relays[RELAY_COUNT];
static SemaphoreHandle_t s_mutex = NULL;

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

esp_err_t relay_init() {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }

  for (int i = 0; i < RELAY_COUNT; i++) {
    s_relays[i].gpio = RELAY_GPIOS[i];

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIOS[i]),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_GPIOS[i], 1);
  }
  relay_nvs_load();
  xTaskCreate(relay_schedule_task, "relay_sched", 2048, NULL, 5, NULL);
  ESP_LOGI(TAG, "Relay module initialized");
  return ESP_OK;
}

esp_err_t relay_set_state(uint8_t relay_num, bool on) {
  if (relay_num < 1 || relay_num > RELAY_COUNT) {
    return ESP_ERR_INVALID_ARG;
  }

  int i = relay_num - 1;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_relays[i].is_on = on;
  gpio_set_level(s_relays[i].gpio, on ? 0 : 1);
  xSemaphoreGive(s_mutex);

  ESP_LOGI(TAG, "Relay %d -> %s", relay_num, on ? "ON" : "OFF");
  return ESP_OK;
}

static void relay_schedule_task(void* arg) {
  while (1) {
    if (time_is_synced()) {
      time_t now = time(NULL);
      struct tm local_time;
      localtime_r(&now, &local_time);
      struct tm* t = &local_time;
      uint16_t current_minute = (uint16_t)(t->tm_hour * 60 + t->tm_min);

      ESP_LOGI(TAG, "Parsed time: %02d:%02d", t->tm_hour, t->tm_min);
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      for (int i = 0; i < RELAY_COUNT; i++) {
        relay_schedule_t* sch = &s_relays[i].schedule;
        if (!sch->enabled) continue;
        bool should_be_on = relay_schedule_is_active(sch, current_minute);
        if (s_relays[i].is_on != should_be_on) {
          gpio_set_level(s_relays[i].gpio, should_be_on ? 0 : 1);
          s_relays[i].is_on = should_be_on;
          ESP_LOGI(TAG, "Relay %d -> %s (schedule)", i + 1,
                   should_be_on ? "ON" : "OFF");
        }
      }
      xSemaphoreGive(s_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

relay_state_t* relay_get_state(uint8_t relay_num) {
  if (relay_num < 1 || relay_num > RELAY_COUNT) return NULL;
  return &s_relays[relay_num - 1];
}

esp_err_t relay_get_state_handler(httpd_req_t* req) {
  cJSON* root = cJSON_CreateArray();
  if (root == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  for (int i = 0; i < RELAY_COUNT; i++) {
    relay_state_t* state = &s_relays[i];
    cJSON* relay = cJSON_CreateObject();
    cJSON* schedule = cJSON_CreateObject();
    cJSON* start = cJSON_CreateObject();
    cJSON* stop = cJSON_CreateObject();

    if (relay == NULL || schedule == NULL || start == NULL || stop == NULL) {
      cJSON_Delete(relay);
      cJSON_Delete(schedule);
      cJSON_Delete(start);
      cJSON_Delete(stop);
      xSemaphoreGive(s_mutex);
      cJSON_Delete(root);
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }

    cJSON_AddNumberToObject(relay, "relay", i + 1);
    cJSON_AddBoolToObject(relay, "is_on", state->is_on);
    cJSON_AddNumberToObject(relay, "gpio", state->gpio);
    cJSON_AddBoolToObject(schedule, "enabled", state->schedule.enabled);
    cJSON_AddNumberToObject(start, "hour", state->schedule.start.hour);
    cJSON_AddNumberToObject(start, "minute", state->schedule.start.minute);
    cJSON_AddNumberToObject(stop, "hour", state->schedule.stop.hour);
    cJSON_AddNumberToObject(stop, "minute", state->schedule.stop.minute);
    cJSON_AddItemToObject(schedule, "start", start);
    cJSON_AddItemToObject(schedule, "stop", stop);
    cJSON_AddItemToObject(relay, "schedule", schedule);
    cJSON_AddItemToArray(root, relay);
  }
  xSemaphoreGive(s_mutex);

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

esp_err_t relay_handler_set(httpd_req_t* req) {
  char* buf = malloc(req->content_len + 1);
  if (buf == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  int received = httpd_req_recv(req, buf, req->content_len);
  if (received <= 0) {
    free(buf);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_OK;
  }
  buf[received] = '\0';

  cJSON* json = cJSON_Parse(buf);
  free(buf);

  if (json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_OK;
  }
  cJSON* j_relay = cJSON_GetObjectItem(json, "relay");
  cJSON* j_state = cJSON_GetObjectItem(json, "state");

  if (!cJSON_IsNumber(j_relay) || !cJSON_IsBool(j_state)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fields");
    return ESP_OK;
  }

  int relay_num = (int)cJSON_GetNumberValue(j_relay);
  bool state = cJSON_IsTrue(j_state);
  cJSON_Delete(json);

  esp_err_t err = relay_set_state(relay_num, state);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay number");
    return ESP_OK;
  }

  relay_nvs_save(relay_num);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}

esp_err_t relay_handler_schedule(httpd_req_t* req) {
  char* buf = malloc(req->content_len + 1);
  if (buf == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int received = httpd_req_recv(req, buf, req->content_len);
  if (received <= 0) {
    free(buf);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_OK;
  }
  buf[received] = '\0';

  cJSON* json = cJSON_Parse(buf);
  free(buf);
  if (json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_OK;
  }

  cJSON* j_relay = cJSON_GetObjectItem(json, "relay");
  cJSON* j_enabled = cJSON_GetObjectItem(json, "enabled");
  cJSON* j_start = cJSON_GetObjectItem(json, "start");
  cJSON* j_stop = cJSON_GetObjectItem(json, "stop");

  if (!cJSON_IsNumber(j_relay) || !cJSON_IsBool(j_enabled)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid fields");
    return ESP_OK;
  }

  int relay_num = (int)cJSON_GetNumberValue(j_relay);
  if (relay_num < 1 || relay_num > RELAY_COUNT) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid relay number");
    return ESP_OK;
  }

  int i = relay_num - 1;
  bool enabled = cJSON_IsTrue(j_enabled);
  if (!enabled) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_relays[i].schedule.enabled = false;
    s_relays[i].is_on = false;
    gpio_set_level(s_relays[i].gpio, 1);
    xSemaphoreGive(s_mutex);
    ESP_LOGI(TAG, "Relay %d schedule disabled", relay_num);
    relay_nvs_save(relay_num);
    cJSON_Delete(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
  }
  if (!cJSON_IsObject(j_start) || !cJSON_IsObject(j_stop)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing start/stop");
    return ESP_OK;
  }

  cJSON* j_start_hour = cJSON_GetObjectItem(j_start, "hour");
  cJSON* j_start_minute = cJSON_GetObjectItem(j_start, "minute");
  cJSON* j_stop_hour = cJSON_GetObjectItem(j_stop, "hour");
  cJSON* j_stop_minute = cJSON_GetObjectItem(j_stop, "minute");
  if (!cJSON_IsNumber(j_start_hour) || !cJSON_IsNumber(j_start_minute) ||
      !cJSON_IsNumber(j_stop_hour) || !cJSON_IsNumber(j_stop_minute) ||
      cJSON_GetNumberValue(j_start_hour) < 0 ||
      cJSON_GetNumberValue(j_start_hour) > 23 ||
      cJSON_GetNumberValue(j_start_minute) < 0 ||
      cJSON_GetNumberValue(j_start_minute) > 59 ||
      cJSON_GetNumberValue(j_stop_hour) < 0 ||
      cJSON_GetNumberValue(j_stop_hour) > 23 ||
      cJSON_GetNumberValue(j_stop_minute) < 0 ||
      cJSON_GetNumberValue(j_stop_minute) > 59) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid schedule time");
    return ESP_OK;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_relays[i].schedule.enabled = true;
  s_relays[i].schedule.start.hour = (uint8_t)cJSON_GetNumberValue(j_start_hour);
  s_relays[i].schedule.start.minute =
      (uint8_t)cJSON_GetNumberValue(j_start_minute);
  s_relays[i].schedule.stop.hour = (uint8_t)cJSON_GetNumberValue(j_stop_hour);
  s_relays[i].schedule.stop.minute =
      (uint8_t)cJSON_GetNumberValue(j_stop_minute);
  xSemaphoreGive(s_mutex);
  relay_nvs_save(relay_num);
  cJSON_Delete(json);

  ESP_LOGI(TAG, "Relay %d schedule: %02d:%02d → %02d:%02d | enabled: %d",
           relay_num, s_relays[i].schedule.start.hour,
           s_relays[i].schedule.start.minute, s_relays[i].schedule.stop.hour,
           s_relays[i].schedule.stop.minute, s_relays[i].schedule.enabled);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}

void relay_emergency_stop(uint8_t relay_num) {
  if (relay_num < 1 || relay_num > RELAY_COUNT) return;

  int i = relay_num - 1;

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_relays[i].is_on = false;
  s_relays[i].schedule.enabled = false;
  gpio_set_level(s_relays[i].gpio, 1);
  xSemaphoreGive(s_mutex);

  // comment this to avoid saving the state during emergency stop
  // relay_nvs_save(relay_num);
}

static void relay_nvs_save(uint8_t relay_num) {
  char key[16];
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;

  int i = relay_num - 1;

  snprintf(key, sizeof(key), "r%d_state", relay_num);
  nvs_set_u8(handle, key, s_relays[i].is_on ? 1 : 0);

  if (!s_relays[i].schedule.enabled) {
    snprintf(key, sizeof(key), "r%d_en", relay_num);
    nvs_erase_key(handle, key);
    snprintf(key, sizeof(key), "r%d_sh", relay_num);
    nvs_erase_key(handle, key);
    snprintf(key, sizeof(key), "r%d_sm", relay_num);
    nvs_erase_key(handle, key);
    snprintf(key, sizeof(key), "r%d_eh", relay_num);
    nvs_erase_key(handle, key);
    snprintf(key, sizeof(key), "r%d_em", relay_num);
    nvs_erase_key(handle, key);
    nvs_commit(handle);
    nvs_close(handle);
    return;
  }

  snprintf(key, sizeof(key), "r%d_en", relay_num);
  nvs_set_u8(handle, key, (uint8_t)s_relays[i].schedule.enabled);
  snprintf(key, sizeof(key), "r%d_sh", relay_num);
  nvs_set_u8(handle, key, s_relays[i].schedule.start.hour);
  snprintf(key, sizeof(key), "r%d_sm", relay_num);
  nvs_set_u8(handle, key, s_relays[i].schedule.start.minute);
  snprintf(key, sizeof(key), "r%d_eh", relay_num);
  nvs_set_u8(handle, key, s_relays[i].schedule.stop.hour);
  snprintf(key, sizeof(key), "r%d_em", relay_num);
  nvs_set_u8(handle, key, s_relays[i].schedule.stop.minute);

  nvs_commit(handle);
  nvs_close(handle);
}

static void relay_nvs_load(void) {
  nvs_handle_t handle;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;

  for (int i = 0; i < RELAY_COUNT; i++) {
    uint8_t relay_num = i + 1;
    char key[16];
    uint8_t val = 0;

    snprintf(key, sizeof(key), "r%d_state", relay_num);
    if (nvs_get_u8(handle, key, &val) == ESP_OK) {
      s_relays[i].is_on = val != 0;
      gpio_set_level(s_relays[i].gpio, s_relays[i].is_on ? 0 : 1);
    } else {
      s_relays[i].is_on = false;
    }

    snprintf(key, sizeof(key), "r%d_en", relay_num);
    if (nvs_get_u8(handle, key, &val) == ESP_OK) {
      s_relays[i].schedule.enabled = (bool)val;
      snprintf(key, sizeof(key), "r%d_sh", relay_num);
      if (nvs_get_u8(handle, key, &val) == ESP_OK) {
        s_relays[i].schedule.start.hour = val;
      }
      snprintf(key, sizeof(key), "r%d_sm", relay_num);
      if (nvs_get_u8(handle, key, &val) == ESP_OK) {
        s_relays[i].schedule.start.minute = val;
      }
      snprintf(key, sizeof(key), "r%d_eh", relay_num);
      if (nvs_get_u8(handle, key, &val) == ESP_OK) {
        s_relays[i].schedule.stop.hour = val;
      }
      snprintf(key, sizeof(key), "r%d_em", relay_num);
      if (nvs_get_u8(handle, key, &val) == ESP_OK) {
        s_relays[i].schedule.stop.minute = val;
      }
    }
  }
  nvs_close(handle);
}