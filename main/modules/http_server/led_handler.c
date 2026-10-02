#include "led_handler.h"

#include "cJSON.h"
#include "esp_err.h"
#include "led_status.h"

#include "led_handler.h"

#include "cJSON.h"
#include "esp_err.h"
#include "led_status.h"

esp_err_t led_handler(httpd_req_t* req) {
  if (req->content_len == 0 || req->content_len > 128) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
    return ESP_OK;
  }

  char buf[129];
  int received = httpd_req_recv(req, buf, req->content_len);
  if (received <= 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
    return ESP_OK;
  }
  buf[received] = '\0';

  cJSON* json = cJSON_Parse(buf);
  if (json == NULL) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return ESP_OK;
  }

  cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
  if (!cJSON_IsBool(enabled)) {
    cJSON_Delete(json);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "enabled must be boolean");
    return ESP_OK;
  }

  led_status_enable(cJSON_IsTrue(enabled));
  cJSON_Delete(json);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}