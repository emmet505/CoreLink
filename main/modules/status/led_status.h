#ifndef LED_STATUS_H
#define LED_STATUS_H

#include "stdbool.h"
#include "esp_err.h"



typedef enum {
    LED_ERR_NONE       = 0,
    LED_ERR_WIFI       = (1 << 0),
    LED_ERR_FILESYSTEM = (1 << 1),
    LED_ERR_HEAP       = (1 << 2),
    LED_ERR_HTTP       = (1 << 3),
    LED_ERR_ESTOP      = (1 << 4),
} led_error_t;

typedef enum {
    LED_WARN_NONE = 0,
    LED_WARN_HEAP = (1 << 0),
} led_warning_t;


esp_err_t led_status_init(void);
void led_status_raise(led_error_t err);     
void led_status_clear(led_error_t err);    
void led_status_warn(led_warning_t warn);    
void led_status_clear_warn(led_warning_t warn); 
void led_status_enable(bool on);

bool led_status_is_enabled(void);


#endif