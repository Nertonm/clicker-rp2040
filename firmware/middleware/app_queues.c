/**
 * @file app_queues.c
 * @brief Implementação da criação de filas para o FreeRTOS.
 *
 * Gerencia a alocação de memória para as filas de mensagens utilizadas
 * na comunicação inter-tarefas.
 */

#include "app_queues.h"
#include "config/firmware_config.h"

/** @brief Handle privado para a fila de cliques. */
static QueueHandle_t queue_clicks;

void app_queues_init(void) {
  /* Cria a fila com o comprimento definido nas configurações */
  queue_clicks = xQueueCreate(CLICK_QUEUE_LEN, sizeof(click_msg_t));

  /* Garante que a fila foi criada com sucesso */
  configASSERT(queue_clicks != NULL);
}

QueueHandle_t app_queues_get_clicks(void) { return queue_clicks; }
