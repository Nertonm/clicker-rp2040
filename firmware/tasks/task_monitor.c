/**
 * @file task_monitor.c
 * @author
 * @date 2026-03-01
 * @brief Implementação da tarefa de monitoramento de integridade do sistema.
 *
 * Realiza telemetria periódica via UART, reportando o estado das filas,
 * uso de memória heap e status de conectividade.
 */

#include "task_monitor.h"
#include "FreeRTOS.h"
#include "task.h"
#include "config/firmware_config.h"
#include "middleware/app_queues.h"
#include "middleware/shared_state.h"
#include <stdio.h>

/**
 * @brief Loop principal da tarefa de monitoramento.
 *
 * @param[in] param Parâmetro opcional da tarefa (não utilizado).
 */
void task_monitor(void *param) {
  (void)param;

  QueueHandle_t queue_clicks = app_queues_get_clicks();

  while (1) {
    /* Coleta métricas do sistema */
    UBaseType_t depth = uxQueueMessagesWaiting(queue_clicks);
    size_t free_heap = xPortGetFreeHeapSize();
    size_t min_heap = xPortGetMinimumEverFreeHeapSize();

    /* Reporta telemetria via serial para depuração */
    printf("[MON] queue=%lu heap=%lu min=%lu conn=%d\n", (unsigned long)depth,
           (unsigned long)free_heap, (unsigned long)min_heap,
           (int)shared_state_get_connection_status());

    /* Aguarda o próximo ciclo de monitoramento definido em firmware_config.h */
    vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));
  }
}
