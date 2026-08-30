#include <stdio.h>
#include <stdlib.h>

#include "captive_portal.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "modules/filesystem/filesystem.h"
#include "modules/http_server/http_server.h"
#include "modules/system_monitor/system_monitor.h"
#include "modules/wifi/wifi.h"
#include "nvs_flash.h"
#include "utils/Network/dns_server.h"
#include "wifi_config_store.h"

static const char* TAG = "APP";

static void monitor_task(void* pvParameters);

static void heap_stress_task(void* pvParameters);

static void monitor_task(void* pvParameters) {
  while (1) {
    // system_monitor_check_health();
    int clients = wifi_get_connected_clients();
    ESP_LOGI(TAG, "Connected clients: %d | running on core %d", clients,
             xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Simple IoT House");
//nvs_flash_erase();
  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Erasing NVS and reinitializing");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "NVS ready");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_LOGI(TAG, "Network stack ready");

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_LOGI(TAG, "Event loop ready");

  ESP_ERROR_CHECK(led_status_init());

  
  esp_err_t err = wifi_init();
  if (err != ESP_OK){
    led_status_raise(LED_ERR_WIFI);
    ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
  }
  ESP_ERROR_CHECK(filesystem_init());
  ESP_ERROR_CHECK(http_server_start());

  xTaskCreate(monitor_task, "monitor", 2048, NULL, tskIDLE_PRIORITY, NULL);

  // xTaskCreate(heap_stress_task, "heap_stress", 4096, NULL, tskIDLE_PRIORITY,
  //             NULL);

  ESP_LOGI(TAG, "Monitor task started");

  ESP_LOGI(TAG, "Device is ready");
  ESP_LOGI(TAG, "CPU cores available: %d", portNUM_PROCESSORS);

  // system_monitor_print();
  // system_status_t status;
  // system_monitor_get_status(&status);
  // ESP_LOGI(TAG, "Free Heap      : %u", status.free_heap);
  // ESP_LOGI(TAG, "Total Heap     : %u", status.total_heap);
  // ESP_LOGI(TAG, "Min Free Heap  : %u", status.minimum_free_heap);
  // ESP_LOGI(TAG, "Heap Usage     : %.1f %%", status.heap_usage);

  // ESP_LOGI(TAG, "CPU Cores      : %lu", status.cpu_cores);

  // ESP_LOGI(TAG, "Internal RAM   : %u KB", status.internal / 1024);

  // ESP_LOGI(TAG, "PSRAM          : %zu KB", status.psram_size / 1024);
  // ESP_LOGI(TAG, "Flash_size     : %zu KB ", status.flash_size / 1024);
}

// static void test_runtime_stats(void) {
//   UBaseType_t task_count = uxTaskGetNumberOfTasks();
//   ESP_LOGI(TAG, "Number of tasks: %u", (unsigned)task_count);
// }

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