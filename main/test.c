#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void heap_stress_task(void* pvParameters);

const static char* TAG = "TEST";
static void heap_stress_task(void* pvParameters) {
  void* blocks[300] = {0};
  int count = 0;
  bool filling = true;
  while (1) {
    if (filling) {
      blocks[count] = heap_caps_malloc(1 * 1024, MALLOC_CAP_INTERNAL);
      if (blocks[count] != NULL) {
        count++;
        ESP_LOGI(TAG, "Allocated block %d", count);
      } else {
        filling = false;
        ESP_LOGW(TAG, "Memory full at block %d, releasing...", count);
      }
      if (count >= 300) filling = false;
    } else {
      if (count > 0) {
        count--;
        free(blocks[count]);
        blocks[count] = NULL;
        ESP_LOGI(TAG, "Free block %d", count);
      }
      if (count == 0) filling = true;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}