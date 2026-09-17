#include "time_handler.h"

#include <stdlib.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "time.h"
#define TIME_REQ_LEN 256
#define NTP_WAIT_TIMEOUT_MS 3000

#ifndef CONFIG_TIMEZONE
#define CONFIG_TIMEZONE "IRST-3:30"
#endif

#ifndef CONFIG_WIFI_NTP_SERVER_1
#define CONFIG_WIFI_NTP_SERVER_1 "0.asia.pool.ntp.org"
#endif

#ifndef CONFIG_WIFI_NTP_SERVER_2
#define CONFIG_WIFI_NTP_SERVER_2 "0.asia.pool.ntp.org"
#endif

#ifndef CONFIG_WIFI_NTP_SERVER_3
#define CONFIG_WIFI_NTP_SERVER_3 "0.asia.pool.ntp.org"
#endif

static const char* TAG = "TIME";
static bool s_ntp_synced = false;
static bool s_time_available = false;
static bool s_ntp_started = false;

typedef struct {
  char server[3][64];
  char timezone[64];
} time_settings_t;

static time_settings_t s_time_settings;

static void time_settings_defaults(time_settings_t* settings) {
  snprintf(settings->server[0], sizeof(settings->server[0]), "%s",
           CONFIG_WIFI_NTP_SERVER_1);
  snprintf(settings->server[1], sizeof(settings->server[1]), "%s",
           CONFIG_WIFI_NTP_SERVER_2);
  snprintf(settings->server[2], sizeof(settings->server[2]), "%s",
           CONFIG_WIFI_NTP_SERVER_3);
  snprintf(settings->timezone, sizeof(settings->timezone), "%s",
           CONFIG_TIMEZONE);
}

static esp_err_t time_settings_load(time_settings_t* settings) {
  time_settings_defaults(settings);
  nvs_handle_t handle;
  esp_err_t err = nvs_open("time_cfg", NVS_READONLY, &handle);
  if (err != ESP_OK) return ESP_OK;

  const char* keys[] = {"server1", "server2", "server3", "timezone"};
  char* values[] = {settings->server[0], settings->server[1],
                    settings->server[2], settings->timezone};
  size_t sizes[] = {sizeof(settings->server[0]), sizeof(settings->server[1]),
                    sizeof(settings->server[2]), sizeof(settings->timezone)};
  for (int i = 0; i < 4; i++) {
    err = nvs_get_str(handle, keys[i], values[i], &sizes[i]);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
      nvs_close(handle);
      return err;
    }
  }
  nvs_close(handle);
  return ESP_OK;
}

static esp_err_t time_settings_save(const time_settings_t* settings) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open("time_cfg", NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;

  const char* keys[] = {"server1", "server2", "server3", "timezone"};
  const char* values[] = {settings->server[0], settings->server[1],
                          settings->server[2], settings->timezone};
  for (int i = 0; i < 4; i++) {
    err = nvs_set_str(handle, keys[i], values[i]);
    if (err != ESP_OK) {
      nvs_close(handle);
      return err;
    }
  }
  err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

static void ntp_sync_callback(struct timeval* tv) {
  (void)tv;

  s_ntp_synced = true;
  s_time_available = true;
  ESP_LOGI(TAG, "Time synchronized from NTP");
}

bool time_is_synced(void) { return s_time_available; }

esp_err_t time_start_ntp(void) {
  if (s_ntp_started) return ESP_OK;

  esp_err_t err = time_settings_load(&s_time_settings);
  if (err != ESP_OK) return err;

  setenv("TZ", s_time_settings.timezone, 1);
  tzset();

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
      3,
      ESP_SNTP_SERVER_LIST(s_time_settings.server[0], s_time_settings.server[1],
                           s_time_settings.server[2]));
  config.sync_cb = ntp_sync_callback;

  err = esp_netif_sntp_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize SNTP: %s", esp_err_to_name(err));
    return err;
  }

  s_ntp_started = true;
  ESP_LOGI(TAG, "SNTP started with 3 servers");
  return ESP_OK;
}

esp_err_t time_settings_get_handler(httpd_req_t* req) {
  time_settings_t settings;
  esp_err_t err = time_settings_load(&settings);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to load time settings: %s", esp_err_to_name(err));
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON* root = cJSON_CreateObject();
  if (root == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON_AddStringToObject(root, "server1", settings.server[0]);
  cJSON_AddStringToObject(root, "server2", settings.server[1]);
  cJSON_AddStringToObject(root, "server3", settings.server[2]);
  cJSON_AddStringToObject(root, "timezone", settings.timezone);

  char* json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (json == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
  free(json);
  return ESP_OK;
}

esp_err_t time_settings_post_handler(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > 512) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid body size\"}");
    return ESP_OK;
  }

  char body[513];
  int received = httpd_req_recv(req, body, sizeof(body) - 1);
  if (received <= 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Empty body\"}");
    return ESP_OK;
  }
  body[received] = '\0';

  cJSON* root = cJSON_Parse(body);
  if (root == NULL) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return ESP_OK;
  }

  time_settings_t settings;
  if (time_settings_load(&settings) != ESP_OK) {
    cJSON_Delete(root);
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  const char* values[4];
  cJSON* fields[] = {cJSON_GetObjectItem(root, "server1"),
                     cJSON_GetObjectItem(root, "server2"),
                     cJSON_GetObjectItem(root, "server3"),
                     cJSON_GetObjectItem(root, "timezone")};
  char* destinations[] = {settings.server[0], settings.server[1],
                          settings.server[2], settings.timezone};
  size_t limits[] = {sizeof(settings.server[0]), sizeof(settings.server[1]),
                     sizeof(settings.server[2]), sizeof(settings.timezone)};
  for (int i = 0; i < 4; i++) {
    if (!cJSON_IsString(fields[i]) ||
        strlen(cJSON_GetStringValue(fields[i])) == 0 ||
        strlen(cJSON_GetStringValue(fields[i])) >= limits[i]) {
      cJSON_Delete(root);
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(req, "{\"error\":\"Invalid NTP settings\"}");
      return ESP_OK;
    }
    values[i] = cJSON_GetStringValue(fields[i]);
    strlcpy(destinations[i], values[i], limits[i]);
  }
  cJSON_Delete(root);

  if (time_settings_save(&settings) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  if (s_ntp_started) {
    esp_netif_sntp_deinit();
    s_ntp_started = false;
    s_ntp_synced = false;
  }
  time_start_ntp();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true,\"reboot\":false}");
  return ESP_OK;
}

void time_print_current(void) {
  char buf[12];
  time_get_str(buf, sizeof(buf));
  const char* source = !s_time_available
                           ? "not synchronized"
                           : (s_ntp_synced ? "NTP" : "client fallback");
  ESP_LOGI(TAG, "Current time: %s (%s)", s_time_available ? buf : "unavailable",
           source);
}

void time_get_str(char* buf, size_t len) {
  time_t now = time(NULL);
  struct tm local_time;
  localtime_r(&now, &local_time);
  snprintf(buf, len, "%02d:%02d:%02d", local_time.tm_hour, local_time.tm_min,
           local_time.tm_sec);
}

esp_err_t time_sync_handler(httpd_req_t* req) {
  if (s_ntp_synced) {
    httpd_resp_set_status(req, "409 Conflict");
    httpd_resp_sendstr(req, "{\"error\":\"NTP time is already available\"}");
    return ESP_OK;
  }

  if (s_ntp_started) {
    esp_err_t ntp_err =
        esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NTP_WAIT_TIMEOUT_MS));
    if (ntp_err == ESP_OK || s_ntp_synced) {
      httpd_resp_set_status(req, "409 Conflict");
      httpd_resp_sendstr(req, "{\"error\":\"NTP time is available\"}");
      return ESP_OK;
    }
    ESP_LOGW(TAG, "NTP unavailable, accepting client time fallback");
  }

  if (req->content_len > TIME_REQ_LEN) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Body too large\"}");
    return ESP_OK;
  }

  char* buf = malloc(req->content_len + 1);
  if (buf == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  int recieved = httpd_req_recv(req, buf, req->content_len);
  if (recieved <= 0) {
    free(buf);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Empty body\"}");
    return ESP_OK;
  }
  buf[recieved] = '\0';

  cJSON* json = cJSON_Parse(buf);
  free(buf);

  if (json == NULL) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
    return ESP_OK;
  }

  cJSON* j_hour = cJSON_GetObjectItem(json, "hour");
  cJSON* j_minute = cJSON_GetObjectItem(json, "minute");
  cJSON* j_second = cJSON_GetObjectItem(json, "second");

  if (!cJSON_IsNumber(j_hour) || !cJSON_IsNumber(j_minute) ||
      !cJSON_IsNumber(j_second)) {
    cJSON_Delete(json);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Missing time fields\"}");
    return ESP_OK;
  }

  int hour = (int)cJSON_GetNumberValue(j_hour);
  int minute = (int)cJSON_GetNumberValue(j_minute);
  int second = (int)cJSON_GetNumberValue(j_second);
  cJSON_Delete(json);

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 59) {
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"error\":\"Invalid time values\"}");
    return ESP_OK;
  }

  time_t now = time(NULL);
  struct tm t = {0};
  localtime_r(&now, &t);
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;

  time_t timestamp = mktime(&t);
  if (timestamp == (time_t)-1) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  struct timeval tv = {.tv_sec = timestamp, .tv_usec = 0};
  if (settimeofday(&tv, NULL) != 0) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  s_time_available = true;
  ESP_LOGI(TAG, "Time set from client fallback: %02d:%02d:%02d", hour, minute,
           second);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}