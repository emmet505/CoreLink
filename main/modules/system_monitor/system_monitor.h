#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
  /* Heap */
  size_t free_heap;
  size_t total_heap;
  size_t minimum_free_heap;
  float heap_usage;

  /* Hardware */
  uint32_t cpu_cores;
  uint32_t chip_revision;

  /* Memory */
  size_t internal_ram_size;
  size_t psram_size;

  /*flash size*/
  size_t flash_size;

} system_status_t;

void system_monitor_print();
void system_monitor_get_status(system_status_t* status);