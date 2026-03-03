/**
 * @file main.c
 * @brief Ponto de entrada principal do firmware Clicker RP2040.
 *
 * Este arquivo contém a função main(), responsável pela inicialização básica
 * do hardware (stdio, GPIOs, barramentos I2C), criação das tarefas do
 * FreeRTOS e início do escalonador do kernel.
 *
 * @author
 * @date 2026-03-01
 */

#include "FreeRTOS.h"
#include "task.h"

#include "audio/buzzer.h"
#include "debug_log.h"
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

/**
 * @brief Macro para cronometrar etapas de boot.
 * Exibe via LOG_NORMAL o nome da etapa e o tempo decorrido em ms.
 */
#define BOOT_STEP(label, call)                                              \
  do {                                                                      \
    absolute_time_t _t0_ = get_absolute_time();                             \
    call;                                                                   \
    uint32_t _dt_ = (uint32_t)(                                             \
        absolute_time_diff_us(_t0_, get_absolute_time()) / 1000u);          \
    LOG_NORMAL("[INIT]", "%-22s OK dt=%lu ms", label, (unsigned long)_dt_); \
  } while (0)

/**
 * @brief Função principal (Ponto de entrada).
 *
 * Realiza a orquestração do boot:
 * 1. Inicializa subsistemas de hardware.
 * 2. Prepara o estado compartilhado e filas.
 * 3. Cria as tarefas operacionais.
 * 4. Inicia o escalonador do FreeRTOS.
 *
 * @return int Retorna zero apenas em caso de erro crítico (o scheduler nunca deve retornar).
 */
int main(void) {
  /* Inicialização da biblioteca padrão e periféricos básicos */
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  sleep_ms(1500);

  printf("\n[INIT] ===== Clicker RP2040 - Sprint 2 =====\n");
  printf("[INIT] FreeRTOS: %s  NODE_ID=%d  DEBUG_LEVEL=%d\n",
         tskKERNEL_VERSION_NUMBER, NODE_ID, DEBUG_LEVEL);
  printf("[INIT] Heap configurado: %u bytes\n", (unsigned)configTOTAL_HEAP_SIZE);

  absolute_time_t boot_start = get_absolute_time();

  /* --- Inicialização de Middlewares e Drivers (com timing) --- */
  BOOT_STEP("shared_state_init",   shared_state_init());
  BOOT_STEP("lamport_init",        lamport_init());
  BOOT_STEP("button_handler_init", button_handler_init());
  BOOT_STEP("display_init",        display_init());
  BOOT_STEP("led_matrix_init",     led_matrix_init());
  BOOT_STEP("buzzer_init",         buzzer_init());
  BOOT_STEP("app_queues_init",     app_queues_init());

  /* --- Criação das Tarefas do FreeRTOS --- */
  BaseType_t ok;

  /* Tarefa de processamento de botões (Alta Prioridade) */
  ok = xTaskCreate(task_buttons, "buttons", 512, NULL, 3, NULL);
  configASSERT(ok == pdPASS);
  LOG_NORMAL("[INIT]", "task buttons criada priority=3 stack=512");

  /* Tarefa de rede RPC (Prioridade Média) */
  ok = xTaskCreate(task_rpc, "rpc", 4096, NULL, 2, NULL);
  configASSERT(ok == pdPASS);
  LOG_NORMAL("[INIT]", "task rpc criada priority=2 stack=4096");

  /* Tarefa de atualização da interface visual (Baixa Prioridade) */
  ok = xTaskCreate(task_display, "display", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);
  LOG_NORMAL("[INIT]", "task display criada priority=1 stack=1024");

  /* Tarefa de monitoramento do sistema (Baixa Prioridade) */
  ok = xTaskCreate(task_monitor, "monitor", 768, NULL, 1, NULL);
  configASSERT(ok == pdPASS);
  LOG_NORMAL("[INIT]", "task monitor criada priority=1 stack=768");

#ifdef STRESS_TEST
  stress_test_start();
#endif

  uint32_t total_boot_ms = (uint32_t)(
      absolute_time_diff_us(boot_start, get_absolute_time()) / 1000u);
  LOG_NORMAL("[INIT]", "Boot completo total=%lu ms heap_livre=%u bytes",
             (unsigned long)total_boot_ms, (unsigned)xPortGetFreeHeapSize());
  LOG_NORMAL("[INIT]", "Iniciando scheduler FreeRTOS...");

  vTaskStartScheduler();

  /* Se chegou aqui, houve falta de memória ou erro crítico no kernel */
  LOG_ERROR("[ERROR]", "SCHEDULER RETORNOU — ERRO FATAL (sem memoria?)");
  while (1) {
    tight_loop_contents();
  }
}
