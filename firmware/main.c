#include "FreeRTOS.h"
#include "task.h"

#include "audio/buzzer.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "middleware/app_queues.h"
#include "middleware/button_handler.h"
#include "middleware/lamport.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"
#include "tasks/task_buttons.h"
#include "tasks/task_display.h"
#include "tasks/task_monitor.h"
#include "tasks/task_rpc.h"

#include <stdio.h>

#ifdef STRESS_TEST
#include "tests/stress_test.h"
#endif

int main(void) {
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  sleep_ms(1500);

  printf("\n[BOOT] ===== Clicker RP2040 - Sprint 2 =====\n");
  printf("[BOOT] FreeRTOS version: %s\n", tskKERNEL_VERSION_NUMBER);

  shared_state_init();
  lamport_init();
  button_handler_init();
  display_init();
  led_matrix_init();
  buzzer_init();
  app_queues_init();

  BaseType_t ok;
  ok = xTaskCreate(task_buttons, "buttons", 512, NULL, 3, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_rpc, "rpc", 4096, NULL, 2, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_display, "display", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_monitor, "monitor", 768, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

#ifdef STRESS_TEST
  stress_test_start();
#endif

  printf("[BOOT] Scheduler iniciado\n");
  vTaskStartScheduler();

  printf("[BOOT] ERRO FATAL: scheduler retornou\n");
  while (1) {
    tight_loop_contents();
  }
}
