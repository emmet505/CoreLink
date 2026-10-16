#include "filesystem.h"

#include <stdio.h>
#include <sys/stat.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "led_status.h"
#include "nvs.h"

static const char* TAG = "filesystem";
static const char* LABELS[2] = {"webfs_a", "webfs_b"};
static int s_active = -1;

const char* filesystem_slot_label(int slot) { return LABELS[slot & 1]; }
int filesystem_active_slot(void) { return s_active; }

static int slot_load(void) {
  nvs_handle_t h;
  uint8_t slot = 0;
  if (nvs_open("web_cfg", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "slot", &slot);
    nvs_close(h);
  }
  return slot & 1;
}

esp_err_t filesystem_set_boot_slot(int slot) {
  nvs_handle_t h;
  esp_err_t err = nvs_open("web_cfg", NVS_READWRITE, &h);
  if (err != ESP_OK) return err;
  nvs_set_u8(h, "slot", (uint8_t)(slot & 1));
  err = nvs_commit(h);
  nvs_close(h);
  return err;
}

/* mount + چک وجود index.html؛ در شکست، mount را برمی‌گرداند
 */
static esp_err_t mount_slot(int slot, const char* base) {
  esp_vfs_littlefs_conf_t conf = {
      .base_path = base,
      .partition_label = LABELS[slot],
      .format_if_mount_failed = false,
  };
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) return err;

  char path[48];
  snprintf(path, sizeof(path), "%s/index.html", base);
  struct stat st;
  if (stat(path, &st) != 0) {
    esp_vfs_littlefs_unregister(LABELS[slot]);
    return ESP_ERR_NOT_FOUND;
  }
  return ESP_OK;
}

esp_err_t filesystem_verify_slot(int slot) {
  esp_err_t err = mount_slot(slot, "/verify");
  if (err == ESP_OK) esp_vfs_littlefs_unregister(LABELS[slot]);
  return err;
}

esp_err_t filesystem_init(void) {
  int preferred = slot_load();
  for (int k = 0; k < 2; k++) {
    int slot = (preferred + k) & 1;
    if (mount_slot(slot, "/littlefs") == ESP_OK) {
      s_active = slot;
      if (k == 1) filesystem_set_boot_slot(slot); /* slot اصلی خراب بود */
      ESP_LOGI(TAG, "Web UI from %s", LABELS[slot]);
      return ESP_OK;
    }
    ESP_LOGW(TAG, "%s unusable", LABELS[slot]);
  }
  led_status_raise(LED_ERR_FILESYSTEM);
  return ESP_FAIL;
}