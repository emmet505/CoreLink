#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "system_monitor.h"

#define FILE_PATH_MAX 512
#define FILE_BUFFER_SIZE 512
static const char* TAG = "http_server";

/* forward declarations for handlers used before their definitions */
static esp_err_t status_api_handler(httpd_req_t* req);

static esp_err_t serve_file(httpd_req_t* req, const char* path) {
  FILE* file = fopen(path, "r");
  if (file == NULL) {
    ESP_LOGE(TAG, "File not found: %s", path);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  const char* ext = strrchr(path, '.');
  if (ext != NULL && strcmp(ext, ".css") == 0) {
    httpd_resp_set_type(req, "text/css");
  } else if (ext != NULL && strcmp(ext, ".js") == 0) {
    httpd_resp_set_type(req, "application/javascript");
  } else {
    httpd_resp_set_type(req, "text/html");
  }

  char buffer[FILE_BUFFER_SIZE];
  size_t read_bytes;
  while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to send chunk");
      fclose(file);
      return ESP_FAIL;
    }
  }

  fclose(file);
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t* req) {
  return serve_file(req, "/littlefs/index.html");
}

static esp_err_t static_handler(httpd_req_t* req) {
  char path[FILE_PATH_MAX];
  const char* uri = req->uri;

  if (snprintf(path, sizeof(path), "/littlefs%s", uri) >= sizeof(path)) {
    ESP_LOGE(TAG, "Path too long");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  return serve_file(req, path);
}

esp_err_t http_server_start(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  ESP_LOGI(TAG, "Starting HTTP server");

  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "HTTP server start failed");
    return ESP_FAIL;
  }

  httpd_uri_t root_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &root_uri);

  httpd_uri_t css_uri = {
      .uri = "/style.css",
      .method = HTTP_GET,
      .handler = static_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &css_uri);

  httpd_uri_t js_uri = {
      .uri = "/script.js",
      .method = HTTP_GET,
      .handler = static_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &js_uri);

  httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_api_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &status_uri);

  return ESP_OK;
}

static esp_err_t status_api_handler(httpd_req_t* req) {
  system_status_t status;
  system_monitor_get_status(&status);
  cJSON* root = cJSON_CreateObject();

  if (root == NULL) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  cJSON_AddNumberToObject(root, "heap_total", status.total_heap);
  cJSON_AddNumberToObject(root, "heap_free", status.free_heap);
  cJSON_AddNumberToObject(root, "heap_used", status.used_heap);
  cJSON_AddNumberToObject(root, "heap_min_free", status.minimum_free_heap);
  cJSON_AddNumberToObject(root, "heap_usage", status.heap_usage);

  cJSON_AddNumberToObject(root, "internal_heap_total",
                          status.internal_heap_total);
  cJSON_AddNumberToObject(root, "internal_heap_free",
                          status.internal_heap_free);
  cJSON_AddNumberToObject(root, "internal_heap_used",
                          status.internal_heap_used);
  cJSON_AddNumberToObject(root, "internal_heap_usage",
                          status.internal_heap_usage);

  cJSON_AddNumberToObject(root, "psram_total", status.psram_total);
  cJSON_AddNumberToObject(root, "psram_free", status.psram_free);
  cJSON_AddNumberToObject(root, "psram_used", status.psram_used);
  cJSON_AddNumberToObject(root, "psram_usage", status.psram_usage);

  cJSON_AddNumberToObject(root, "cpu_cores", status.cpu_cores);
  cJSON_AddNumberToObject(root, "flash_size", status.flash_size);
  cJSON_AddNumberToObject(root,"uptime_seconds",status.uptime_seconds);

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
