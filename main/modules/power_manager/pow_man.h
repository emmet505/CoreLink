#pragma once

#include "esp_err.h"
esp_err_t reset_button_init(void);
void pow_man_factory_reset(void);
void pow_man_reboot(void);
void power_manager_emergency_stop(void);
