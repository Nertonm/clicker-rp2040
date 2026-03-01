#include "task_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "config/firmware_config.h"
#include "middleware/app_queues.h"
#include "middleware/shared_state.h"
#include <stdio.h>

void task_monitor(void *param) {
  (void)param;

  QueueHandle_t queue_clicks = app_queues_get_clicks();

  while (1) {
    UBaseType_t depth = uxQueueMessagesWaiting(queue_clicks);
    size_t free_heap = xPortGetFreeHeapSize();
    size_t min_heap = xPortGetMinimumEverFreeHeapSize();

    printf("[MON] queue=%lu heap=%lu min=%lu conn=%d\n", (unsigned long)depth,
           (unsigned long)free_heap, (unsigned long)min_heap,
           (int)shared_state_get_connection_status());

    vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));
  }
}
