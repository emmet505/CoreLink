#pragma once

#include "esp_err.h"

esp_err_t filesystem_init(void);
int filesystem_active_slot(void);
const char* filesystem_slot_label(int slot);
esp_err_t filesystem_verify_slot(int slot);
esp_err_t filesystem_set_boot_slot(int slot);