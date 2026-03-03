/**
 * @file shared_state_reader.h
 * @brief Interface de leitura do estado compartilhado para o Core 1.
 *
 * Provê uma camada de abstração que expõe apenas funções de consulta (getters),
 * garantindo que o Core 1 não realize escritas acidentais no estado global.
 */

#ifndef SHARED_STATE_READER_H
#define SHARED_STATE_READER_H

#include "shared_state.h"
#include <stdbool.h>
#include <stdint.h>

/** @brief Obtém a pontuação local atualizada. */
uint32_t shared_state_get_local_score(void);

/** @brief Obtém a pontuação global acumulada. */
uint32_t shared_state_get_global_score(void);

/** @brief Obtém as pontuações individuais de todos os nós. */
void shared_state_get_node_scores(uint32_t *out, uint8_t count);

/** @brief Consulta o estado atual da conexão de rede. */
connection_status_t shared_state_get_connection_status(void);

/** @brief Lê e limpa atomicamente a flag de marco (milestone). */
bool shared_state_take_milestone_triggered(void);

/** @brief Lê e limpa atomicamente a solicitação de flash do LED. */
bool shared_state_take_led_flash_requested(void);

#endif /* SHARED_STATE_READER_H */
