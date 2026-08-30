#pragma once

#include "led_status.h"

#include <stddef.h>
#include <stdint.h>
void system_monitor_check_health(void);

typedef struct {

    /* Heap */
    size_t free_heap;
    size_t total_heap;
    size_t used_heap;
    size_t minimum_free_heap;
    float heap_usage;

    /* Hardware */
    uint32_t cpu_cores;
    uint32_t chip_revision;

    /* Memory */
    size_t internal_heap_total;
    size_t internal_heap_free;
    size_t internal_heap_used;
    float internal_heap_usage;

    size_t psram_total;
    size_t psram_free;
    size_t psram_used;
    float psram_usage;

    /* Flash */
    size_t flash_size;

    /* system */
    int64_t uptime_seconds;

    /* CPU */
    float cpu_usage_core0;
    float cpu_usage_core1;

} system_status_t;


int64_t esp_timer_get_time(void);
void system_monitor_print();
void system_monitor_get_status(system_status_t* status);
