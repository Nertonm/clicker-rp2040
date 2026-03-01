/**
 * @file lamport.h
 * @brief Relógio lógico de Lamport para ordenação causal de eventos
 */

#ifndef LAMPORT_H
#define LAMPORT_H

#include <stdint.h>

/**
 * Inicializa o relógio de Lamport.
 * Deve ser chamado uma vez no boot.
 */
void lamport_init(void);

/**
 * Incrementa e retorna o timestamp Lamport atual.
 * Chamar antes de cada envio de evento.
 *
 * @return Timestamp Lamport atual (após incremento)
 */
int lamport_tick(void);

/**
 * Atualiza o relógio local com timestamp autoritativo do servidor.
 * O servidor já calcula max(local, received) + 1, então apenas aceita.
 *
 * @param received_ts Timestamp Lamport retornado pelo servidor
 */
void lamport_update(int received_ts);

/**
 * Retorna o valor atual do relógio sem incrementar.
 *
 * @return Timestamp Lamport atual
 */
int lamport_get(void);

#endif // LAMPORT_H
