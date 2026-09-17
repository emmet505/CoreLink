#ifndef WIFI_MODULE_H
#define WIFI_MODULE_H

#include "esp_err.h"

/**
 * @brief Starting Up WiFi AP
 * @return ESP_OK 
 */
esp_err_t wifi_init(void);

/**
 * @brief Stop WiFi
 */
void wifi_stop(void);

/**
 * @brief Connected Clients count
 * @return number of connected clients
 */
uint8_t wifi_get_connected_clients(void);

bool wifi_is_sta_connected(void);

#endif 