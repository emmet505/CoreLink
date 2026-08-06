#include "system_monitor.h"

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_flash.h"
#include "esp_system.h"


// void system_monitor_print() {
//   size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
//   size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
//   float usage = (float)(total_heap - free_heap) / total_heap * 100.0f;

//   ESP_LOGI(TAG, "Free Heap : %u", (unsigned)free_heap);
//   ESP_LOGI(TAG, "Total Heap: %u", (unsigned)total_heap);
//   ESP_LOGI(TAG, "Usage     : %.2f%%", usage);
// }

void system_monitor_get_status(system_status_t* status) {
  if (status == NULL) {
    return;
  }

  status->free_heap = esp_get_free_heap_size();

  status->total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

  status->minimum_free_heap = esp_get_minimum_free_heap_size();

  status->heap_usage =
      100.0f - ((float)status->free_heap * 100.0f / (float)status->total_heap);

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  status->cpu_cores = chip_info.cores;
  status->chip_revision = chip_info.revision;

  status->internal_ram_size = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

  status->psram_size = esp_psram_get_size();

  uint32_t flash_size = 0;
  if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
    status->flash_size = flash_size;
  } else {
    status->flash_size = 0; // Unable to get flash size
  }

}