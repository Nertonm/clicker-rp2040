/**
 * @file task_buttons.c
 * @author
 * @date 2026-03-01
 * @brief Implementação da tarefa de processamento de cliques.
 *
 * Esta tarefa consome os cliques pendentes registrados via interrupção (ISR)
 * no shared_state, atualiza as pontuações locais e globais e encaminha
 * os dados para a fila de transmissão RPC.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "middleware/app_queues.h"
#include "middleware/game_events.h"
#include "middleware/shared_state.h"
#include "task_buttons.h"

/**
 * @brief Loop principal da tarefa de botões.
 *
 * @param[in] param Parâmetro opcional da tarefa (não utilizado).
 */
void task_buttons(void *param) {
  (void)param;

  /* Obtém o handle da fila de comunicação com a task_rpc */
  QueueHandle_t queue_clicks = app_queues_get_clicks();

  while (1) {
    /* Consome cliques pendentes de forma atômica do shared_state */
    uint32_t pending = shared_state_take_pending_clicks();
    
    if (pending > 0) {
      /* Solicita feedback visual imediato (flash de LED) */
      shared_state_set_led_flash_requested(true);

      /* Prepara mensagem para a fila RPC */
      click_msg_t msg = {.clicks = pending};
      
      if (xQueueSend(queue_clicks, &msg, 0) == pdPASS) {
        /* Atualização otimista do score local antes da confirmação do servidor */
        uint32_t local_now = shared_state_get_local_score();
        uint32_t global_now = shared_state_get_global_score();
        uint32_t local_next = local_now + pending;

        shared_state_set_local_score(local_next);
        shared_state_set_global_score(global_now + pending);

        /* Verifica se atingiu um novo múltiplo de 10 para feedback de marco */
        if ((local_now / 10u) < (local_next / 10u)) {
          trigger_milestone_feedback();
        }
      } else {
        /* Caso a fila esteja cheia, devolve os cliques para o estado pendente */
        shared_state_restore_clicks(pending);
      }
    }

    /* Intervalo de poll da tarefa (20ms) */
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
