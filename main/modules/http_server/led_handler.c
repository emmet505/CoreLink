#include "led_handler.h"

#include "cJSON.h"
#include "esp_err.h"
#include "http_utils.h"
#include "led_status.h"

#define LED_BODY_MAX 128

/* POST /api/led  {"enabled": true|false} */
esp_err_t led_handler(httpd_req_t* req) {
  cJSON* json = http_read_json(req, LED_BODY_MAX);
  if (json == NULL) return ESP_OK;  // error response already sent

  const cJSON* enabled = cJSON_GetObjectItem(json, "enabled");
  if (!cJSON_IsBool(enabled)) {
    cJSON_Delete(json);
    return http_send_json_error(req, "400 Bad Request",
                                "enabled must be boolean");
  }

  led_status_enable(cJSON_IsTrue(enabled));
  cJSON_Delete(json);

  return http_send_json_ok(req);
  return ESP_OK;
}