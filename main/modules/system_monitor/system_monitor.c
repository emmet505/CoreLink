#include <stdlib.h>
#include <string.h>

#include "system_monitor.h"

#include "led_status.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
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

  /* General Heap */

  status->free_heap = esp_get_free_heap_size();

  status->total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);

  status->used_heap = status->total_heap - status->free_heap;

  status->minimum_free_heap = esp_get_minimum_free_heap_size();

  status->heap_usage =
      ((float)status->used_heap * 100.0f) / (float)status->total_heap;

  /* CPU / Chip */

  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);

  status->cpu_cores = chip_info.cores;
  status->chip_revision = chip_info.revision;

  /* Internal RAM Heap */

  status->internal_heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);

  status->internal_heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

  status->internal_heap_used =
      status->internal_heap_total - status->internal_heap_free;

  status->internal_heap_usage = ((float)status->internal_heap_used * 100.0f) /
                                (float)status->internal_heap_total;

  /* PSRAM */

  status->psram_total = esp_psram_get_size();

  status->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

  status->psram_used = status->psram_total - status->psram_free;

  if (status->psram_total > 0) {
    status->psram_usage =
        ((float)status->psram_used * 100.0f) / (float)status->psram_total;
  } else {
    status->psram_usage = 0;
  }

  /* Flash */

  uint32_t flash_size = 0;
  status->flash_size =
      (esp_flash_get_size(NULL, &flash_size) == ESP_OK) ? flash_size : 0;

  /* system */
  status->uptime_seconds = (uint64_t)(esp_timer_get_time() / 1000000ULL);
}


void system_monitor_check_health(void) {
    system_status_t status;
    system_monitor_get_status(&status);

    if (status.internal_heap_usage >= 85.0f) {
        led_status_clear_warn(LED_WARN_HEAP);
        led_status_raise(LED_ERR_HEAP);
    } else if (status.internal_heap_usage >= 70.0f) {
        led_status_clear(LED_ERR_HEAP);
        led_status_warn(LED_WARN_HEAP);
    } else {
        led_status_clear(LED_ERR_HEAP);
        led_status_clear_warn(LED_WARN_HEAP);
    }
}