#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

void vAssertCalled(const char *file, int line) {
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ASSERT FAILED em %s:%d\n", file, line);
  while (1) {
  }
}

void vApplicationStackOverflowHook(TaskHandle_t task_handle,
                                   char *task_name) {
  (void)task_handle;
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ERRO FATAL: Stack overflow na task '%s'\n",
         task_name ? task_name : "<unknown>");
  while (1) {
  }
}

void vApplicationMallocFailedHook(void) {
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ERRO FATAL: pvPortMalloc falhou (heap livre=%u bytes)\n",
         (unsigned)xPortGetFreeHeapSize());
  printf("[FREERTOS] Dica: aumente configTOTAL_HEAP_SIZE em FreeRTOSConfig.h\n");
  while (1) {
  }
}

void vApplicationIdleHook(void) {
}
