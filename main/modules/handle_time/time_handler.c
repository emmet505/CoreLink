#include "time_handler.h"

#include <stdlib.h>
#include <sys/time.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "time.h"
#define TIME_REQ_LEN 256

static const char* TAG = "TIME";
static bool s_synced = false;

bool time_is_synced(void) { return s_synced; }

void time_print_current(void) {
  char buf[12];
  time_get_str(buf, sizeof(buf));
  ESP_LOGI(TAG, "Current time: %s", s_synced ? buf : "not synced");
}


void time_get_str(char* buf, size_t len) {
  time_t now = time(NULL);
  struct tm* t = localtime(&now);
  snprintf(buf, len, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
}

esp_err_t time_sync_handler(httpd_req_t* req) {
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

  struct tm t = {0};
  t.tm_hour = hour;
  t.tm_min  = minute;
  t.tm_sec  = second;
  t.tm_year = 70;
  t.tm_mon  = 0;
  t.tm_mday = 0;

  struct timeval tv = {.tv_sec = mktime(&t), .tv_usec = 0 };
  settimeofday(&tv, NULL);
    s_synced = true;
    ESP_LOGI(TAG, "Time synced: %02d:%02d:%02d", hour, minute, second);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;

}