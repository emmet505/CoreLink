#ifndef WIFI_CONFIG_STORE_H
#define WIFI_CONFIG_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
  char ssid[33];
  char password[65];
  uint8_t channel;
  uint8_t max_connections;
  char ip[16];
  char gateway[16];
  char netmask[16];
} wifi_config_store_t;

esp_err_t wifi_config_load(wifi_config_store_t* out_cfg);
esp_err_t wifi_config_save(const wifi_config_store_t* cfg);
esp_err_t wifi_config_erase(void);

#endif
