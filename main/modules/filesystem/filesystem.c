#include "filesystem.h"

#include "esp_littlefs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include "led_status.h"


static const char* TAG = "filesystem";

static void filesystem_check(const char* label) {
  struct stat st;
  if (stat("/littlefs/index.html", &st) != 0) { // if exists, stat returns 0, otherwise -1
    ESP_LOGE(TAG, "index.html missing in LittleFS");
    led_status_raise(LED_ERR_FILESYSTEM);
  }

  size_t total = 0, used = 0;
  if (esp_littlefs_info(label, &total, &used) == ESP_OK) {
    ESP_LOGI(TAG, "LittleFS: %zu / %zu bytes used", used, total);
    if (used * 100 > total * 90) {
      ESP_LOGW(TAG, "LittleFS almost full");
    }
  }
}


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

  filesystem_check(conf.partition_label);

  return ESP_OK;
}