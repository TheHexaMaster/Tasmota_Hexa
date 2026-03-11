/// RTOS ///

// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <string.h>

#include "esp_timer.h"

#ifdef USE_WEB_STATUS_LINE_CPU

#if (configGENERATE_RUN_TIME_STATS == 1) && (INCLUDE_xTaskGetIdleTaskHandle == 1)
#define USE_WEB_CPU_LOAD_SAFE 1
#endif

#ifdef USE_WEB_CPU_LOAD_SAFE

struct CpuLoadCoreSample {
  volatile uint8_t load_pct;
  volatile bool valid;
};

static CpuLoadCoreSample g_cpu_load[configNUMBER_OF_CORES];
static TaskHandle_t g_cpu_load_task[configNUMBER_OF_CORES] = { nullptr };

static void CpuLoadSamplerTask(void *param) {
  uint32_t core = (uint32_t)(uintptr_t)param;

  // nech sa counters najprv trochu rozbehnú
  vTaskDelay(pdMS_TO_TICKS(1500));

  uint32_t last_idle_us = ulTaskGetIdleRunTimeCounter();
  uint64_t last_wall_us = (uint64_t)esp_timer_get_time();

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    uint32_t now_idle_us = ulTaskGetIdleRunTimeCounter();
    uint64_t now_wall_us = (uint64_t)esp_timer_get_time();

    uint32_t idle_delta_us = (uint32_t)(now_idle_us - last_idle_us);   // wrap-safe
    uint32_t wall_delta_us = (uint32_t)(now_wall_us - last_wall_us);   // 1s interval => safe

    last_idle_us = now_idle_us;
    last_wall_us = now_wall_us;

    if (wall_delta_us >= 10000U) {
      uint32_t idle_pct = (idle_delta_us >= wall_delta_us) ? 100U : ((idle_delta_us * 100U) / wall_delta_us);
      uint32_t load_pct = 100U - idle_pct;
      if (load_pct > 100U) { load_pct = 100U; }

      g_cpu_load[core].load_pct = (uint8_t)load_pct;
      g_cpu_load[core].valid = true;
    }
  }
}

static void CpuLoadSamplerStart(void) {
  static bool started = false;
  if (started) { return; }
  started = true;

  for (uint32_t core = 0; core < configNUMBER_OF_CORES; core++) {
    char name[16];
    snprintf(name, sizeof(name), "cpu_load_%u", core);

    xTaskCreatePinnedToCore(
      CpuLoadSamplerTask,
      name,
      2048,
      (void*)(uintptr_t)core,
      1,                       // nízka priorita, ale nie idle
      &g_cpu_load_task[core],
      core
    );
  }
}

#endif
#endif
////////////