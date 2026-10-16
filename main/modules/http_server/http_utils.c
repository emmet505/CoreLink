#include "http_utils.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

static const char* TAG = "http_utils";

#define RECV_MAX_TIMEOUTS 3

esp_err_t http_send_json_error(httpd_req_t* req, const char* status,
                               const char* msg) {
  char body[128];
  snprintf(body, sizeof(body), "{\"success\":false,\"error\":\"%s\"}", msg);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, body);
  return ESP_OK;
}

esp_err_t http_send_json_ok(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"success\":true}");
  return ESP_OK;
}

char* http_read_body(httpd_req_t* req, size_t max_len) {
  size_t len = req->content_len;
  if (len == 0 || len > max_len) {
    http_send_json_error(req, "400 Bad Request", "Invalid body size");
    return NULL;
  }

  char* buf = malloc(len + 1);
  if (buf == NULL) {
    ESP_LOGE(TAG, "malloc(%u) failed", (unsigned)(len + 1));
    http_send_json_error(req, "500 Internal Server Error", "Out of memory");
    return NULL;
  }

  size_t got = 0;  // bytes received so far
  int timeouts = 0;
  while (got < len) {
    int rec = httpd_req_recv(req, buf + got, len - got);
    if (rec == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < RECV_MAX_TIMEOUTS) {
      continue;  // retry a couple of times before giving up
    }
    if (rec <= 0) {
      free(buf);
      http_send_json_error(req, "400 Bad Request", "Receive failed");
      return NULL;
    }
    got += (size_t)rec;
  }
  buf[got] = '\0';
  return buf;
}

cJSON* http_read_json(httpd_req_t* req, size_t max_len) {
  char* buf = http_read_body(req, max_len);
  if (buf == NULL) return NULL;  // error already sent

  cJSON* json = cJSON_Parse(buf);
  free(buf);  // cJSON copies what it needs
  if (json == NULL) {
    http_send_json_error(req, "400 Bad Request", "Invalid JSON");
  }
  return json;
}