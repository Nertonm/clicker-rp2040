#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Número máximo de nós suportados na rede.
 */
#define MAX_NODES 3

/**
 * @brief Estados possíveis da conexão.
 */
typedef enum {
  STATUS_CONNECTING,
  STATUS_ONLINE,
  STATUS_OFFLINE,
  STATUS_SYNCING
} connection_status_t;

/**
 * @brief Inicializa o estado compartilhado e o spinlock.
 * Deve ser chamado pelo Core 0 antes de iniciar o Core 1.
 */
void shared_state_init(void);

/* Funções de Acesso (Thread-safe via Spinlock) */

/** @section Escrita: Core 0 / Leitura: Core 1 */

// Cliques Pendentes
uint32_t shared_state_get_pending_clicks(void);
void shared_state_increment_pending_clicks(void);
uint32_t shared_state_take_pending_clicks(
    void); // Lê e zera atomicamente (Consumo Core 0)

// Scores
uint32_t shared_state_get_local_score(void);
void shared_state_set_local_score(uint32_t score); // Escrita: Core 0 apenas

uint32_t shared_state_get_global_score(void);
void shared_state_set_global_score(uint32_t score); // Escrita: Core 0 apenas

void shared_state_get_node_scores(uint32_t *out_scores, uint8_t count);
void shared_state_set_node_scores(const uint32_t *in_scores,
                                  uint8_t count); // Escrita: Core 0 apenas

// Status de Conexão
connection_status_t shared_state_get_connection_status(void);
void shared_state_set_connection_status(connection_status_t status);

// Lamport Timestamp
uint32_t shared_state_get_lamport_ts(void);
void shared_state_set_lamport_ts(uint32_t ts);
void shared_state_increment_lamport_ts(void);

// Flags de Eventos
bool shared_state_get_milestone_triggered(void);
void shared_state_set_milestone_triggered(bool triggered);

bool shared_state_get_led_flash_requested(void);
void shared_state_set_led_flash_requested(bool requested);

#endif // SHARED_STATE_H