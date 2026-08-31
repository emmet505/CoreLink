#include "led_status.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "led_strip.h"
#include "stdbool.h"

static const char* TAG = "LED";

#define LED_GPIO 48
#define LED_BRIGHTNESS 15

static led_strip_handle_t s_led      = NULL;
static bool               s_enable   = true;
static TaskHandle_t       s_task     = NULL;
static SemaphoreHandle_t  s_mutex    = NULL;
static uint32_t           s_errors   = LED_ERR_NONE;
static uint32_t           s_warnings = LED_WARN_NONE;


static void led_nvs_save(void);
static void led_nvs_load(void);


static const char *NVS_NAMESPACE = "led_cfg";
static const char *KEY_ENABLED   = "enabled";

static void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  if (s_led == NULL) return;
  led_strip_set_pixel(s_led, 0, r, g, b);
  led_strip_refresh(s_led);
}

static void led_off(void) {
  if (s_led == NULL) return;
  led_strip_clear(s_led);
}

static void led_task(void *arg) {
    bool led_on = false;

    while (1) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        uint32_t errors   = s_errors;
        uint32_t warnings = s_warnings;
        bool     enable   = s_enable;
        xSemaphoreGive(s_mutex);

        if (!enable) {
            led_off();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (errors != LED_ERR_NONE) {
            led_on = !led_on;
            if (led_on) led_set_color(LED_BRIGHTNESS, 0, 0);
            else        led_off();
            vTaskDelay(pdMS_TO_TICKS(250));
        } else if (warnings != LED_WARN_NONE) {
            led_on = !led_on;
            if (led_on) led_set_color(LED_BRIGHTNESS, LED_BRIGHTNESS, 0);
            else        led_off();
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            led_set_color(0, LED_BRIGHTNESS, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t led_status_init(void) {
  s_mutex = xSemaphoreCreateMutex();
  if (s_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create mutex");
    return ESP_FAIL;
  }
  led_nvs_load();
  led_strip_config_t strip_cfg = {
      .strip_gpio_num = LED_GPIO,
      .max_leds = 1,
      .led_model = LED_MODEL_WS2812,
      .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
      .flags = {.invert_out = false}};

  led_strip_rmt_config_t rmt_cfg = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
      .flags = {.with_dma = false},
  };

  esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to init LED strip: %s", esp_err_to_name(err));
    return err;
  }

  led_strip_clear(s_led);

  xTaskCreate(led_task, "led_task", 2048, NULL, 5, &s_task);

  ESP_LOGI(TAG, "LED status initialized");
  return ESP_OK;
}

void led_status_raise(led_error_t err) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_errors |= err;
    xSemaphoreGive(s_mutex);
}

void led_status_clear(led_error_t err) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_errors &= ~err;
    xSemaphoreGive(s_mutex);
}

void led_status_warn(led_warning_t warn) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_warnings |= warn;
    xSemaphoreGive(s_mutex);
}

void led_status_clear_warn(led_warning_t warn) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_warnings &= ~warn;
    xSemaphoreGive(s_mutex);
}

void led_status_enable(bool on) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_enable = on;
    xSemaphoreGive(s_mutex);
    led_nvs_save();
}

bool led_status_is_enabled(void) {
    if (s_mutex == NULL) return true;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool enabled = s_enable;
    xSemaphoreGive(s_mutex);
    return enabled;
}


static void led_nvs_save(void){
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, KEY_ENABLED, (uint8_t) s_enable);
    nvs_commit(handle);
    nvs_close(handle);
}

static void led_nvs_load() {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t enabled = 1; // default enable
    nvs_get_u8(handle,KEY_ENABLED, & enabled);
    s_enable = (bool)enabled;
    nvs_close(handle);
}