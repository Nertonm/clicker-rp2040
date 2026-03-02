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

#include "drivers/display/ws2812.h"
#include "middleware/app_queues.h"
#include "middleware/game_events.h"
#include "middleware/shared_state.h"
#include "task_buttons.h"
#include <stdio.h>

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
    /* Verifica status de conexão */
    connection_status_t status = shared_state_get_connection_status();

    /* Consome cliques pendentes de forma atômica do shared_state */
    uint32_t pending = shared_state_take_pending_clicks();

    if (pending > 0) {
      /* Solicita feedback visual imediato (flash de LED) */
      shared_state_set_led_flash_requested(true);

      /* Se offline ou conectando (sem registro ainda), mantém em pending_clicks.
       * task_rpc não drena queue_clicks enquanto !registered, então não adianta
       * enviar para a fila agora — os clicks ficariam presos e sumiriam do
       * contador de 'pending'. */
      if (status == STATUS_OFFLINE || status == STATUS_CONNECTING) {
        shared_state_restore_clicks(pending);
      } else {
        /* Online/Connecting: envia para fila RPC */
        click_msg_t msg = {.clicks = pending};

        if (xQueueSend(queue_clicks, &msg, 0) != pdPASS) {
          /* Fila cheia: devolve para pending_clicks */
          shared_state_restore_clicks(pending);
        }
      }
    }

    /* Intervalo de poll da tarefa (20ms) */
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
