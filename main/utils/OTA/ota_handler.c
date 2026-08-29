#include "ota_handler.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "string.h"

static const char* TAG = "OTA";

#define OTA_BUFFER_SIZE 4096

esp_err_t ota_firmware_handler(httpd_req_t* req) {
  const esp_partition_t* update_partition =
      esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG, "No OTA partition found");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "No OTA partition");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Writing to partition: %s", update_partition->label);

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err =
      esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "OTA begin failed");
    return ESP_FAIL;
  }

  char* buffer = malloc(OTA_BUFFER_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    esp_ota_abort(ota_handle);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    return ESP_FAIL;
  }

  int received = 0;
  int remaining = req->content_len;

  while (remaining > 0) {
    int chunk_size =
        (remaining > OTA_BUFFER_SIZE) ? OTA_BUFFER_SIZE : remaining;
    received = httpd_req_recv(req, buffer, chunk_size);
    if (received <= 0) {
      ESP_LOGE(TAG, "Failed to receive chunk");
      free(buffer);
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Receive failed");
      return ESP_FAIL;
    }
    err = esp_ota_write(ota_handle, buffer, received);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      free(buffer);
      esp_ota_abort(ota_handle);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
      return ESP_FAIL;
    }

    remaining -= received;
  }

  free(buffer);

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
             esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Set boot partition failed");
    return ESP_FAIL;
  }

  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  ESP_LOGI(TAG, "OTA successful, rebooting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}

esp_err_t ota_webfs_handler(httpd_req_t* req) {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "webfs");
  if (partition == NULL) {
    ESP_LOGE(TAG, "webfs partition not found");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "Partition not found");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Found partition: %s, size: %lu", partition->label,
           partition->size);

  esp_err_t err = esp_partition_erase_range(partition, 0, partition->size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to erase partition: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Erase failed");
    return ESP_FAIL;
  }
  ESP_LOGI(TAG, "Partition erased successfully");

  char* buffer = malloc(OTA_BUFFER_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    return ESP_FAIL;
  }

  int remaining = req->content_len;
  uint32_t offset = 0;

  while (remaining > 0) {
    int chunk_size =
        (remaining > OTA_BUFFER_SIZE) ? OTA_BUFFER_SIZE : remaining;
    int received = httpd_req_recv(req, buffer, chunk_size);
    if (received <= 0) {
      ESP_LOGE(TAG, "Failed to receive chunk");
      free(buffer);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "Receive failed");
      return ESP_FAIL;
    }

    err = esp_partition_write(partition, offset, buffer, received);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_partition_write failed: %s", esp_err_to_name(err));
      free(buffer);
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
      return ESP_FAIL;
    }

    offset += received;
    remaining -= received;
  }

  free(buffer);

  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  ESP_LOGI(TAG, "WebFS OTA successful, rebooting...");
  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();

  return ESP_OK;
}
