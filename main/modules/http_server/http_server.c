#include "http_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "captive_portal.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/ip4_addr.h"
#include "system_monitor.h"
#include "wifi_config_store.h"

#define FILE_PATH_MAX 512
#define FILE_BUFFER_SIZE 512
#define SETTINGS_BODY_MAX 512
static const char* TAG = "http_server";

/* forward declarations for handlers used before their definitions */
static esp_err_t status_api_handler(httpd_req_t* req);
static esp_err_t relay_schedule_handler(httpd_req_t* req);
static esp_err_t settings_get_handler(httpd_req_t* req);
static esp_err_t settings_post_handler(httpd_req_t* req);

/* ---------------------------------------------------------------
 * Relay schedule state — written by POST /api/relay/schedule
 * Read by application logic to decide relay output.
 * --------------------------------------------------------------- */
typedef struct {
  bool enabled;
  char start[6]; /* "HH:MM" null-terminated */
  char stop[6];
  char device_time[9]; /* "HH:MM:SS" — last known time from browser */
} relay_schedule_t;

static relay_schedule_t s_schedule = {
    .enabled = false,
    .start = "00:00",
    .stop = "00:00",
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
  ESP_LOGI(TAG, "ROOT REQUEST: %s", req->uri);
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

static httpd_handle_t s_server = NULL;
esp_err_t http_server_start(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = 6;
  config.lru_purge_enable = true;
  config.max_uri_handlers = 32;
  config.uri_match_fn = httpd_uri_match_wildcard;

  ESP_LOGI(TAG, "Starting HTTP server");

  if (httpd_start(&s_server, &config) != ESP_OK) {
    ESP_LOGE(TAG, "HTTP server start failed");
    return ESP_FAIL;
  }

  httpd_uri_t root_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(s_server, &root_uri);

  httpd_uri_t css_uri = {
      .uri = "/style.css",
      .method = HTTP_GET,
      .handler = static_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(s_server, &css_uri);

  httpd_uri_t js_uri = {
      .uri = "/script.js",
      .method = HTTP_GET,
      .handler = static_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(s_server, &js_uri);

  httpd_uri_t status_uri = {
      .uri = "/api/status",
      .method = HTTP_GET,
      .handler = status_api_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(s_server, &status_uri);

  httpd_uri_t schedule_uri = {
      .uri = "/api/relay/schedule",
      .method = HTTP_POST,
      .handler = relay_schedule_handler,
      .user_ctx = NULL,
  };
  httpd_register_uri_handler(s_server, &schedule_uri);

  httpd_uri_t settings_get_uri = {.uri = "/api/settings",
                                  .method = HTTP_GET,
                                  .handler = settings_get_handler,
                                  .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &settings_get_uri);

  httpd_uri_t settings_post_uri = {.uri = "/api/settings",
                                   .method = HTTP_POST,
                                   .handler = settings_post_handler,
                                   .user_ctx = NULL};
  httpd_register_uri_handler(s_server, &settings_post_uri);

  esp_err_t cp_err = captive_portal_register(s_server);
  if (cp_err != ESP_OK) {
    ESP_LOGE(TAG, "captive portal register failed");
    return cp_err;
  }
  return ESP_OK;
}

httpd_handle_t http_server_get_handle(void) { return s_server; }

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
  int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
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

  cJSON* j_enabled = cJSON_GetObjectItem(root, "enabled");
  cJSON* j_start = cJSON_GetObjectItem(root, "start");
  cJSON* j_stop = cJSON_GetObjectItem(root, "stop");
  cJSON* j_device_time = cJSON_GetObjectItem(root, "device_time");

  if (!cJSON_IsBool(j_enabled) || !cJSON_IsString(j_start) ||
      !cJSON_IsString(j_stop) || !cJSON_IsString(j_device_time)) {
    cJSON_Delete(root);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                        "Missing or invalid fields");
    return ESP_FAIL;
  }

  s_schedule.enabled = cJSON_IsTrue(j_enabled);
  strlcpy(s_schedule.start, j_start->valuestring, sizeof(s_schedule.start));
  strlcpy(s_schedule.stop, j_stop->valuestring, sizeof(s_schedule.stop));
  strlcpy(s_schedule.device_time, j_device_time->valuestring,
          sizeof(s_schedule.device_time));

  cJSON_Delete(root);

  ESP_LOGI(TAG, "Schedule updated: enabled=%d start=%s stop=%s device_time=%s",
           s_schedule.enabled, s_schedule.start, s_schedule.stop,
           s_schedule.device_time);

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
  if (wifi_config_load(&cfg) != ESP_OK) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  cJSON_AddStringToObject(root, "ssid", cfg.ssid);
  cJSON_AddNumberToObject(root, "channel", cfg.channel);
  cJSON_AddNumberToObject(root, "max_connections", cfg.max_connections);
  cJSON_AddStringToObject(root, "ip", cfg.ip);
  cJSON_AddStringToObject(root, "gateway", cfg.gateway);
  cJSON_AddStringToObject(root, "netmask", cfg.netmask);
  cJSON_AddStringToObject(root, "dns", cfg.dns);
  /* password intentionally omitted */

  char* json = cJSON_PrintUnformatted(root);
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
// POST SETTINGS
static esp_err_t settings_post_handler(httpd_req_t* req) {
  /* 1. Guard against oversized body */
  if (req->content_len > SETTINGS_BODY_MAX) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Body too large\"}");
    return ESP_OK;
  }

  /* 2. Read raw body */
  char body[SETTINGS_BODY_MAX + 1];
  int received = httpd_req_recv(req, body, SETTINGS_BODY_MAX);
  if (received <= 0) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Empty body\"}");
    return ESP_OK;
  }
  body[received] = '\0';
  ESP_LOGI(TAG, "POST /api/settings (%d bytes): %s", received, body);

  /* 3. Parse JSON */
  cJSON* root = cJSON_Parse(body);
  if (!root) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Invalid JSON\"}");
    return ESP_OK;
  }

  /* 4. Extract fields */
  cJSON* j_ssid = cJSON_GetObjectItem(root, "ssid");
  cJSON* j_channel = cJSON_GetObjectItem(root, "channel");
  cJSON* j_max_conn = cJSON_GetObjectItem(root, "max_connections");
  cJSON* j_password = cJSON_GetObjectItem(root, "password");
  cJSON* j_old_password = cJSON_GetObjectItem(root, "old_password");
  cJSON* j_dhcp = cJSON_GetObjectItem(root, "dhcp");
  cJSON* j_ip = cJSON_GetObjectItem(root, "ip");
  cJSON* j_gateway = cJSON_GetObjectItem(root, "gateway");
  cJSON* j_netmask = cJSON_GetObjectItem(root, "netmask");
  cJSON* j_dns = cJSON_GetObjectItem(root, "dns");

  /* 5. Required fields: channel and max_connections always required */
  if (!cJSON_IsNumber(j_channel) || !cJSON_IsNumber(j_max_conn)) {
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_sendstr(
        req,
        "{\"success\":false,\"error\":\"Missing channel or max_connections\"}");
    return ESP_OK;
  }

  int channel = (int)cJSON_GetNumberValue(j_channel);
  int max_conn = (int)cJSON_GetNumberValue(j_max_conn);
  bool dhcp_enabled = cJSON_IsTrue(j_dhcp);

  /* IP fields only required when dhcp=false */
  const char* ip = NULL;
  const char* gateway = NULL;
  const char* netmask = NULL;
  const char* dns = NULL;

  if (!dhcp_enabled) {
    if (!cJSON_IsString(j_ip) || !cJSON_IsString(j_gateway) ||
        !cJSON_IsString(j_netmask)) {
      cJSON_Delete(root);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(req,
                         "{\"success\":false,\"error\":\"ip, gateway and "
                         "netmask required when dhcp is disabled\"}");
      return ESP_OK;
    }
    ip = cJSON_GetStringValue(j_ip);
    gateway = cJSON_GetStringValue(j_gateway);
    netmask = cJSON_GetStringValue(j_netmask);
    if (cJSON_IsString(j_dns)) {
      dns = cJSON_GetStringValue(j_dns);
    }
  }

  /* password: only update if present and non-empty */
  const char* password = NULL;
  if (cJSON_IsString(j_password)) {
    const char* p  = cJSON_GetStringValue(j_password);
    if (p && strlen(p) > 0) {
      password = p;
    }

  }
  const char* ssid = NULL;
  if (cJSON_IsString(j_ssid)) {
    const char* s = cJSON_GetStringValue(j_ssid);
    if (s && strlen(s) > 0) {
      ssid = s;
    }
  }

  /* 6. Validate ranges and formats */
  const char* field_error = NULL;
  esp_ip4_addr_t dummy_addr;

  if (channel < 1 || channel > 13)
    field_error = "channel must be 1-13";
  else if (max_conn < 1 || max_conn > 10)
    field_error = "max_connections must be 1-10";
  else if (ssid && strlen(ssid) > 32)
    field_error = "ssid too long (max 32)";
  else if (password && strlen(password) < 8)
    field_error = "password must be at least 8 characters";
  else if (!dhcp_enabled) {
    if (esp_netif_str_to_ip4(ip, &dummy_addr) != ESP_OK)
      field_error = "invalid ip address";
    else if (esp_netif_str_to_ip4(gateway, &dummy_addr) != ESP_OK)
      field_error = "invalid gateway address";
    else if (esp_netif_str_to_ip4(netmask, &dummy_addr) != ESP_OK)
      field_error = "invalid netmask address";
    else if (dns && esp_netif_str_to_ip4(dns, &dummy_addr) != ESP_OK)
      field_error = "invalid dns address";
  }

  if (field_error) {
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    char err_buf[128];
    snprintf(err_buf, sizeof(err_buf), "{\"success\":false,\"error\":\"%s\"}",
             field_error);
    httpd_resp_sendstr(req, err_buf);
    return ESP_OK;
  }

  /* 7. Load existing config from nvs so untouched fields survive */
  wifi_config_store_t cfg;
  if (wifi_config_load(&cfg) != ESP_OK) {
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(
        req, "{\"success\":false,\"error\":\"Failed to load current config\"}");
    return ESP_OK;
  }
  wifi_config_store_t old_cfg = cfg; /* for logging changes */

  // check old and new password

  if (password) {
    const char* old_pw = cJSON_GetStringValue(j_old_password);
    if (!old_pw || strlen(old_pw) == 0) {
      cJSON_Delete(root);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(
          req, "{\"success\":false,\"error\":\"old_password required\"}");
      
      return ESP_OK;
    }

    if (strcmp(old_pw, cfg.password) != 0) {
      cJSON_Delete(root);
      httpd_resp_set_type(req, "application/json");
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(
          req, "{\"success\":false,\"error\":\"Incorrect old password\"}");
      return ESP_OK;
    }
  }

  /* 8. Overwrite only the fields that were sent */
  cfg.channel = (uint8_t)channel;
  cfg.max_connections = (uint8_t)max_conn;
  cfg.dhcp = dhcp_enabled;

  if (ssid) {
    strlcpy(cfg.ssid, ssid, sizeof(cfg.ssid));
    ESP_LOGI(TAG, "SSID updated: %s", cfg.ssid);
  }

  if (password) {
    strlcpy(cfg.password, password, sizeof(cfg.password));
    ESP_LOGI(TAG, "Password updated");
  } else {
    ESP_LOGI(TAG, "Password unchanged");
  }

  if (!dhcp_enabled) {
    strlcpy(cfg.ip, ip, sizeof(cfg.ip));
    strlcpy(cfg.gateway, gateway, sizeof(cfg.gateway));
    strlcpy(cfg.netmask, netmask, sizeof(cfg.netmask));
    if (dns) {
      strlcpy(cfg.dns, dns, sizeof(cfg.dns));
    }
    ESP_LOGI(TAG, "Static IP: %s / %s / %s", cfg.ip, cfg.gateway, cfg.netmask);
  } else {
    ESP_LOGI(TAG, "DHCP enabled — static IP fields unchanged in NVS");
  }

  cJSON_Delete(root);

  /* 9. Save to NVS */
  esp_err_t err = wifi_config_save(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "wifi_config_save failed: %s", esp_err_to_name(err));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(
        req, "{\"success\":false,\"error\":\"Failed to save settings\"}");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Settings saved — channel=%d max_conn=%d dhcp=%d ssid=%s",
           cfg.channel, cfg.max_connections, cfg.dhcp, cfg.ssid);

  bool needs_reboot = (strcmp(cfg.ssid, old_cfg.ssid) != 0) ||
                      (strcmp(cfg.password, old_cfg.password) != 0) ||
                      (strcmp(cfg.ip, old_cfg.ip) != 0) ||
                      (strcmp(cfg.gateway, old_cfg.gateway) != 0) ||
                      (strcmp(cfg.netmask, old_cfg.netmask) != 0) ||
                      (strcmp(cfg.dns, old_cfg.dns) != 0) ||
                      (cfg.channel != old_cfg.channel) ||
                      (cfg.dhcp != old_cfg.dhcp);

  ESP_LOGI(TAG,
           "Settings saved — channel=%d max_conn=%d dhcp=%d ssid=%s reboot=%d",
           cfg.channel, cfg.max_connections, cfg.dhcp, cfg.ssid, needs_reboot);

  httpd_resp_set_type(req, "application/json");
httpd_resp_sendstr(
    req,
    needs_reboot
        ? "{\"success\":true,\"reboot\":true}"
        : "{\"success\":true,\"reboot\":false}"
);

if (needs_reboot) {
    ESP_LOGW(TAG, "Rebooting in 1500ms due to config change...");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

return ESP_OK;
}
