#include "led_handler.h"

#include "cJSON.h"
#include "esp_err.h"
#include "led_status.h"

esp_err_t led_handler(httpd_req_t* req) {
  char* buf = malloc(req->content_len + 1);

  int received = httpd_req_recv(req, buf, req->content_len);
  if (received <= 0) {
    free(buf);
    return ESP_FAIL;
  }
  buf[req->content_len] = '\0';

  cJSON *json = cJSON_Parse(buf);
  if (json == NULL) {
    free(buf);
    return ESP_FAIL;
  }
  cJSON *enabled = cJSON_GetObjectItem(json, "enabled");
  if (cJSON_IsBool(enabled)) {
    bool value = cJSON_IsTrue(enabled);
    led_status_enable(value);
  } 
  free(buf);
  cJSON_Delete(json);
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}