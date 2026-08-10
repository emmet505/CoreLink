#include "http_server.h"
#include "wifi_config_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "system_monitor.h"

#define FILE_PATH_MAX 512
#define FILE_BUFFER_SIZE 512
#define SETTINGS_BODY_MAX 512
static const char* TAG = "http_server";

/* forward declarations for handlers used before their definitions */
static esp_err_t status_api_handler(httpd_req_t* req);
static esp_err_t relay_schedule_handler(httpd_req_t* req);
static esp_err_t settings_get_handler(httpd_req_t* req);
//static esp_err_t settings_post_handler(httpd_req_t* req);


/* ---------------------------------------------------------------
 * Relay schedule state — written by POST /api/relay/schedule
 * Read by application logic to decide relay output.
 * --------------------------------------------------------------- */
typedef struct {
    bool    enabled;
    char    start[6];   /* "HH:MM" null-terminated */
    char    stop[6];
    char    device_time[9]; /* "HH:MM:SS" — last known time from browser */
} relay_schedule_t;

static relay_schedule_t s_schedule = {
    .enabled     = false,
    .start       = "00:00",
    .stop        = "00:00",
    .device_time = "00:00:00",
};

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

  httpd_uri_t schedule_uri = {
      .uri     = "/api/relay/schedule",
      .method  = HTTP_POST,
      .handler = relay_schedule_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(server, &schedule_uri);

  httpd_uri_t settings_get_uri = {
    .uri       = "/api/settings",
    .method    = HTTP_GET,
    .handler   = settings_get_handler,
    .user_ctx  = NULL
  };
  httpd_register_uri_handler(server, &settings_get_uri);

//  httpd_uri_t settings_post_uri = {
//     .uri       = "/api/settings",
//     .method    = HTTP_POST,
//     .handler   = settings_post_handler,
//     .user_ctx  = NULL
//   };
//   httpd_register_uri_handler(server, &settings_post_uri);
  

  return ESP_OK;
}

/* ── POST /api/relay/schedule ────────────────────────────────────────────────
 *
 * Expected JSON body:
 * {
 *   "enabled":     true | false,
 *   "start":       "HH:MM",
 *   "stop":        "HH:MM",
 *   "device_time": "HH:MM:SS"   <- current browser time for clock sync
 * }
 *
 * Returns: { "ok": true } on success, HTTP 400 on bad input.
 */
static esp_err_t relay_schedule_handler(httpd_req_t* req) {
    char buf[256];
    int  received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    cJSON* root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON* j_enabled     = cJSON_GetObjectItem(root, "enabled");
    cJSON* j_start       = cJSON_GetObjectItem(root, "start");
    cJSON* j_stop        = cJSON_GetObjectItem(root, "stop");
    cJSON* j_device_time = cJSON_GetObjectItem(root, "device_time");

    if (!cJSON_IsBool(j_enabled) ||
        !cJSON_IsString(j_start) ||
        !cJSON_IsString(j_stop)  ||
        !cJSON_IsString(j_device_time)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid fields");
        return ESP_FAIL;
    }

    s_schedule.enabled = cJSON_IsTrue(j_enabled);
    strlcpy(s_schedule.start,       j_start->valuestring,       sizeof(s_schedule.start));
    strlcpy(s_schedule.stop,        j_stop->valuestring,        sizeof(s_schedule.stop));
    strlcpy(s_schedule.device_time, j_device_time->valuestring, sizeof(s_schedule.device_time));

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Schedule updated: enabled=%d start=%s stop=%s device_time=%s",
             s_schedule.enabled, s_schedule.start, s_schedule.stop, s_schedule.device_time);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
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
  cJSON_AddNumberToObject(root, "uptime_seconds", status.uptime_seconds);

  cJSON_AddNumberToObject(root, "cpu_usage_core0", status.cpu_usage_core0);
  cJSON_AddNumberToObject(root, "cpu_usage_core1", status.cpu_usage_core1);

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

/* ── GET /api/settings ───────────────────────────────────────────────────────
 *
 * Returns current Wi-Fi config from NVS as JSON.
 * The dashboard uses this to pre-fill the settings form on page load.
 *
 * Response shape:
 * {
 *   "ssid":            "MyNetwork",
 *   "channel":         1,
 *   "max_connections": 4,
 *   "ip":              "192.168.4.1",
 *   "gateway":         "192.168.4.1",
 *   "netmask":         "255.255.255.0"
 * }
 * Note: password is intentionally omitted from the response. */

 static esp_err_t settings_get_handler(httpd_req_t* req) {
  wifi_config_store_t cfg;
  if(wifi_config_load(&cfg) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON *root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON_AddStringToObject(root, "ssid",            cfg.ssid);
  cJSON_AddNumberToObject(root, "channel",         cfg.channel);
  cJSON_AddNumberToObject(root, "max_connections", cfg.max_connections);
  cJSON_AddStringToObject(root, "ip",              cfg.ip);
  cJSON_AddStringToObject(root, "gateway",         cfg.gateway);
  cJSON_AddStringToObject(root, "netmask",         cfg.netmask);
  /* password intentionally omitted */

  char *json = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!json) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;

 }