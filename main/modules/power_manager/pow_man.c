#include "pow_man.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "wifi_config_store.h"
#include "relay.h"
#include "led_status.h"

static const char* TAG = "ResetBtn";

#define RESET_GPIO GPIO_NUM_14
#define HOLD_MS 10000
#define POLL_MS 100

static void reset_button_task(void* arg) {
  int hold_count = 0;

  while (1) {
    if (gpio_get_level(RESET_GPIO) == 0) {
      hold_count++;
      ESP_LOGI(TAG, "Button held: %d ms", hold_count * POLL_MS);

      if (hold_count * POLL_MS >= HOLD_MS) {
        wifi_config_erase();
        nvs_flash_erase();
        nvs_flash_init();
        esp_restart();
      }

    } else {
      hold_count = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

esp_err_t reset_button_init() {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << RESET_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  xTaskCreate(reset_button_task, "reset_btn", 2048, NULL, 5, NULL);
  return ESP_OK;
}

void pow_man_factory_reset(void){
  wifi_config_erase();
  nvs_flash_erase();
  nvs_flash_init();
  esp_restart();
}

void pow_man_reboot(void){
  esp_restart();
}

void power_manager_emergency_stop(void) {
    ESP_LOGW(TAG, "Emergency stop triggered!");

    for (uint8_t i = 1; i <= 4; i++) {
        relay_emergency_stop(i);
    }

    led_status_raise(LED_ERR_ESTOP);

    ESP_LOGW(TAG, "All relays OFF, schedules disabled");
}