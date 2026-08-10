#include "wifi_config_store.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#ifndef CONFIG_WIFI_DEFAULT_SSID
#define CONFIG_WIFI_DEFAULT_SSID "ESP32-S3-AP"
#endif

#ifndef CONFIG_WIFI_DEFAULT_PASSWORD
#define CONFIG_WIFI_DEFAULT_PASSWORD "12345678"
#endif

#ifndef CONFIG_WIFI_DEFAULT_CHANNEL
#define CONFIG_WIFI_DEFAULT_CHANNEL 1
#endif

#ifndef CONFIG_WIFI_DEFAULT_MAX_CONNECTIONS
#define CONFIG_WIFI_DEFAULT_MAX_CONNECTIONS 4
#endif

#ifndef CONFIG_WIFI_DEFAULT_IP
#define CONFIG_WIFI_DEFAULT_IP "192.168.4.1"
#endif

#ifndef CONFIG_WIFI_DEFAULT_GATEWAY
#define CONFIG_WIFI_DEFAULT_GATEWAY "192.168.4.1"
#endif

#ifndef CONFIG_WIFI_DEFAULT_NETMASK
#define CONFIG_WIFI_DEFAULT_NETMASK "255.255.255.0"
#endif

static const char* TAG = "NVS";
static const char* NVS_NAMESPACE = "wifi_cfg";
static const char* KEY_SSID = "ssid";
static const char* KEY_PASSWORD = "password";
static const char* KEY_CHANNEL = "channel";
static const char* KEY_MAX_CONNECTIONS = "max_conn";
static const char* KEY_IP = "ip";
static const char* KEY_GATEWAY = "gateway";
static const char* KEY_NETMASK = "netmask";

static void set_defaults(wifi_config_store_t* cfg) {
  memset(cfg, 0, sizeof(*cfg));
  snprintf(cfg->ssid, sizeof(cfg->ssid), "%s", CONFIG_WIFI_DEFAULT_SSID);
  snprintf(cfg->password, sizeof(cfg->password), "%s",
           CONFIG_WIFI_DEFAULT_PASSWORD);
  cfg->channel = CONFIG_WIFI_DEFAULT_CHANNEL;
  cfg->max_connections = CONFIG_WIFI_DEFAULT_MAX_CONNECTIONS;
  snprintf(cfg->ip, sizeof(cfg->ip), "%s", CONFIG_WIFI_DEFAULT_IP);
  snprintf(cfg->gateway, sizeof(cfg->gateway), "%s",
           CONFIG_WIFI_DEFAULT_GATEWAY);
  snprintf(cfg->netmask, sizeof(cfg->netmask), "%s",
           CONFIG_WIFI_DEFAULT_NETMASK);
}

esp_err_t wifi_config_load(wifi_config_store_t* out_cfg) {
  if (out_cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  set_defaults(out_cfg);

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGI(TAG, "No saved Wi-Fi config found, using defaults");
    return ESP_OK;
  }

  size_t size;

  size = sizeof(out_cfg->ssid);
  err = nvs_get_str(handle, KEY_SSID, out_cfg->ssid, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(out_cfg->ssid, sizeof(out_cfg->ssid), "%s", CONFIG_WIFI_DEFAULT_SSID);
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  size = sizeof(out_cfg->password);
  err = nvs_get_str(handle, KEY_PASSWORD, out_cfg->password, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(out_cfg->password, sizeof(out_cfg->password), "%s", CONFIG_WIFI_DEFAULT_PASSWORD);
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  uint8_t channel = 0;
  err = nvs_get_u8(handle, KEY_CHANNEL, &channel);
  if (err == ESP_OK) {
    out_cfg->channel = channel;
  } else {
    out_cfg->channel = CONFIG_WIFI_DEFAULT_CHANNEL;
  }

  uint8_t max_connections = 0;
  err = nvs_get_u8(handle, KEY_MAX_CONNECTIONS, &max_connections);
  if (err == ESP_OK) {
    out_cfg->max_connections = max_connections;
  } else {
    out_cfg->max_connections = CONFIG_WIFI_DEFAULT_MAX_CONNECTIONS;
  }

  size = sizeof(out_cfg->ip);
  err = nvs_get_str(handle, KEY_IP, out_cfg->ip, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(out_cfg->ip, sizeof(out_cfg->ip), "%s", CONFIG_WIFI_DEFAULT_IP);
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  size = sizeof(out_cfg->gateway);
  err = nvs_get_str(handle, KEY_GATEWAY, out_cfg->gateway, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(out_cfg->gateway, sizeof(out_cfg->gateway), "%s", CONFIG_WIFI_DEFAULT_GATEWAY);
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  size = sizeof(out_cfg->netmask);
  err = nvs_get_str(handle, KEY_NETMASK, out_cfg->netmask, &size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    snprintf(out_cfg->netmask, sizeof(out_cfg->netmask), "%s", CONFIG_WIFI_DEFAULT_NETMASK);
  } else if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  nvs_close(handle);
  return ESP_OK;
}

esp_err_t wifi_config_save(const wifi_config_store_t* cfg) {
  if (cfg == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_set_str(handle, KEY_SSID, cfg->ssid);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_set_str(handle, KEY_PASSWORD, cfg->password);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_set_u8(handle, KEY_CHANNEL, cfg->channel);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_set_u8(handle, KEY_MAX_CONNECTIONS, cfg->max_connections);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_set_str(handle, KEY_IP, cfg->ip);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_set_str(handle, KEY_GATEWAY, cfg->gateway);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }
  err = nvs_set_str(handle, KEY_NETMASK, cfg->netmask);
  if (err != ESP_OK) {
    nvs_close(handle);
    return err;
  }

  err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

esp_err_t wifi_config_erase(void) {
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_erase_all(handle);
  if (err == ESP_OK) {
    err = nvs_commit(handle);
  }
  nvs_close(handle);
  return err;
}