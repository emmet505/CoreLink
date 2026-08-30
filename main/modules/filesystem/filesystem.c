#include "filesystem.h"

#include "esp_littlefs.h"
#include "esp_log.h"

#include "led_status.h"


static const char* TAG = "filesystem";

esp_err_t filesystem_init(void) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = "/littlefs",
      .partition_label = "webfs",
      .format_if_mount_failed = true,
  };

  esp_err_t ret = esp_vfs_littlefs_register(&conf);

  if (ret != ESP_OK) {
    led_status_raise(LED_ERR_FILESYSTEM);
    ESP_LOGE(TAG, "LittleFS mount failed (%s)", esp_err_to_name(ret));
    return ret;
  }

  size_t total = 0;
  size_t used = 0;

  FILE* f = fopen("/littlefs/index.html", "r");

  if (f) {
    ESP_LOGI(TAG, "index.html exists in LittleFS");
    fclose(f);
  } else {
    ESP_LOGE(TAG, "index.html missing in LittleFS");
  }
  ret = esp_littlefs_info(conf.partition_label, &total, &used);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS total: %d, used: %d", total, used);
  }

  return ESP_OK;
}