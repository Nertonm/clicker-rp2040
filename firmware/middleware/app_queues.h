/**
 * @file app_queues.h
 * @brief Gerenciamento de filas de comunicação entre tarefas FreeRTOS.
 *
 * Provê uma interface centralizada para inicialização e acesso às filas
 * utilizadas para troca de mensagens entre diferentes módulos do sistema.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef APP_QUEUES_H
#define APP_QUEUES_H

#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

/**
 * @brief Estrutura de mensagem para notificação de cliques.
 */
typedef struct {
  uint32_t clicks; /**< Quantidade de cliques a serem processados. */
} click_msg_t;

/**
 * @brief Inicializa todas as filas globais da aplicação.
 *
 * @note Deve ser chamado antes de iniciar o escalonador do FreeRTOS.
 */
void app_queues_init(void);

/**
 * @brief Obtém o handle da fila de cliques.
 *
 * @return QueueHandle_t Handle da fila FreeRTOS para mensagens de tipo click_msg_t.
 */
QueueHandle_t app_queues_get_clicks(void);

#endif /* APP_QUEUES_H */
