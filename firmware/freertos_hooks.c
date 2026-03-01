/**
 * @file freertos_hooks.c
 * @author
 * @date 2026-03-01
 * @brief Implementação dos gatilhos (hooks) do kernel FreeRTOS.
 *
 * Define o comportamento do sistema em casos de erro crítico, como falha
 * de alocação de memória ou estouro de pilha (stack overflow).
 */

#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>

/**
 * @brief Função chamada em caso de falha de assert (configASSERT).
 *
 * @param[in] file Nome do arquivo onde ocorreu a falha.
 * @param[in] line Número da linha da falha.
 */
void vAssertCalled(const char *file, int line) {
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ASSERT FAILED em %s:%d\n", file, line);
  while (1) {
    // Loop de pânico (exige reset manual ou Watchdog)
  }
}

/**
 * @brief Hook chamado quando um estouro de pilha é detectado.
 *
 * @param[in] task_handle Handle da tarefa causadora.
 * @param[in] task_name Nome legível da tarefa causadora.
 */
void vApplicationStackOverflowHook(TaskHandle_t task_handle,
                                   char *task_name) {
  (void)task_handle;
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ERRO FATAL: Stack overflow na task '%s'\n",
         task_name ? task_name : "<unknown>");
  while (1) {
  }
}

/**
 * @brief Hook chamado quando pvPortMalloc falha por falta de memória heap.
 */
void vApplicationMallocFailedHook(void) {
  taskDISABLE_INTERRUPTS();
  printf("[FREERTOS] ERRO FATAL: pvPortMalloc falhou (heap livre=%u bytes)\n",
         (unsigned)xPortGetFreeHeapSize());
  printf("[FREERTOS] Dica: aumente configTOTAL_HEAP_SIZE em FreeRTOSConfig.h\n");
  while (1) {
  }
}

/**
 * @brief Hook chamado durante o tempo ocioso do processador.
 */
void vApplicationIdleHook(void) {
  // Opcional: Entrar em modo de baixo consumo (Sleep)
}
