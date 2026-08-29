#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include "esp_err.h"
#include "esp_http_server.h"

#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t ota_firmware_handler(httpd_req_t *req);
esp_err_t ota_webfs_handler(httpd_req_t *req);

#endif