#include <stdio.h>
#include <stdlib.h>

#include "captive_portal.h"
#include "utils/Network/dns_server.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/filesystem/filesystem.h"
#include "modules/http_server/http_server.h"
#include "modules/system_monitor/system_monitor.h"
#include "modules/wifi/wifi.h"
#include "nvs_flash.h"
#include "wifi_config_store.h"

#include "captive_portal.h"

static const char* TAG = "APP";

static void monitor_task(void* pvParameters);

static void monitor_task(void* pvParameters) {
  while (1) {
    int clients = wifi_get_connected_clients();
    ESP_LOGI(TAG, "Connected clients: %d | running on core %d", clients,
             xPortGetCoreID());
    vTaskDelay(pdMS_TO_TICKS(15000));
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

  ESP_ERROR_CHECK(wifi_init());
  ESP_ERROR_CHECK(filesystem_init());
  ESP_ERROR_CHECK(http_server_start());


  xTaskCreate(monitor_task, "monitor", 2048, NULL, tskIDLE_PRIORITY, NULL);

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
