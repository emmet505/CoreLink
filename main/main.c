#include <stdio.h>
#include <stdlib.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_status.h"
#include "modules/filesystem/filesystem.h"
#include "modules/http_server/http_server.h"
#include "modules/relay/relay.h"
#include "modules/system_monitor/system_monitor.h"
#include "modules/wifi/wifi.h"
#include "nvs_flash.h"
#include "pow_man.h"
#include "time_handler.h"
#include "wifi_config_store.h"

static const char* TAG = "APP";

static void monitor_task(void* pvParameters);
static void cpu_usage_task(void* arg);
static void monitor_task(void* pvParameters) {
  while (1) {
    time_print_current();
    // system_monitor_check_health();
    int clients = wifi_get_connected_clients();
    ESP_LOGI(TAG, "Connected clients: %d | running on core %d", clients,
             xPortGetCoreID());


    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void app_main(void) {
  ESP_LOGI(TAG, "Simple IoT House (core %d)");

  esp_err_t ret = nvs_flash_init();

  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Erasing NVS and reinitializing");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }

  
  
  ESP_ERROR_CHECK(ret);
  ESP_LOGI(TAG, "NVS ready (core %d)");

  ESP_ERROR_CHECK(reset_button_init());
  ESP_LOGI(TAG, "Reset button ready (core %d)");

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_LOGI(TAG, "Network stack ready (core %d)");

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_LOGI(TAG, "Event loop ready (core %d)");

  ESP_ERROR_CHECK(relay_init());

  ESP_ERROR_CHECK(led_status_init());
  ESP_LOGI(TAG, "LED status ready (core %d)");

  esp_err_t err = wifi_init();
  if (err != ESP_OK) {
    led_status_raise(LED_ERR_WIFI);
    ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(err));
  }
  ESP_LOGI(TAG, "WiFi ready (core %d)");

  ESP_ERROR_CHECK(filesystem_init());
  ESP_LOGI(TAG, "Filesystem ready (core %d)");

  ESP_ERROR_CHECK(http_server_start());
  ESP_LOGI(TAG, "HTTP server ready (core %d)");

  xTaskCreate(monitor_task, "monitor", 2048, NULL, tskIDLE_PRIORITY, NULL);

  // Uncomment to heap stress test
  // xTaskCreate(heap_stress_task, "heap_stress", 4096, NULL, tskIDLE_PRIORITY,
  //             NULL);

  ESP_LOGI(TAG, "Monitor task started");

  ESP_LOGI(TAG, "Device is ready");
  ESP_LOGI(TAG, "CPU cores available: %d", portNUM_PROCESSORS);

  xTaskCreate(cpu_usage_task, "cpu_usage", 4096, NULL, tskIDLE_PRIORITY, NULL);

}

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

static void cpu_usage_task(void* arg) {
  while (1) {
    UBaseType_t n = uxTaskGetNumberOfTasks() + 4; // number of tasks + some buffer
    TaskStatus_t* a = malloc(n * sizeof(TaskStatus_t)); // allocate memory for task status array
    TaskStatus_t* b = malloc(n * sizeof(TaskStatus_t));
    if (a == NULL || b == NULL) { free(a); free(b); vTaskDelay(pdMS_TO_TICKS(5000)); continue; }

    configRUN_TIME_COUNTER_TYPE t1, t2;  // variables to hold the total run time counters
    UBaseType_t na = uxTaskGetSystemState(a, n, &t1); // get the system state and total run time at time t1
    vTaskDelay(pdMS_TO_TICKS(1000));
    UBaseType_t nb = uxTaskGetSystemState(b, n, &t2);

    uint32_t total = (uint32_t)(t2 - t1);
    if (total > 0) {
      for (UBaseType_t i = 0; i < na; i++) {
        // uncomment to see tasks name
        // ESP_LOGI("TASKS", "%s", a[i].pcTaskName);
        if (strncmp(a[i].pcTaskName, "IDLE", 4) != 0) continue;
        for (UBaseType_t j = 0; j < nb; j++) {
          if (b[j].xHandle != a[i].xHandle) continue;
          uint32_t idle = (uint32_t)(b[j].ulRunTimeCounter - a[i].ulRunTimeCounter);
          float usage = 100.0f - (idle * 100.0f / total);
          ESP_LOGI("CPU", "%s: busy %.1f%%", a[i].pcTaskName, usage);
        }
      }
    }
    free(a);
    free(b);
    vTaskDelay(pdMS_TO_TICKS(800));
  }
}