/**
 * @file shared_state.c
 * @brief Implementação do gerenciamento de estado compartilhado entre núcleos.
 *
 * Utiliza spinlocks de hardware do RP2040 para garantir que o acesso às
 * variáveis globais de estado seja atômico e seguro entre o Core 0 e Core 1.
 *
 * @author
 * @date 2026-03-01
 */

#include "shared_state.h"
#include "debug_log.h"
#include "hardware/sync.h"
#include <string.h>

/* --- Contadores de diagnóstico do shared_state --- */
static uint32_t diag_status_transitions = 0;
static uint32_t diag_restore_clicks_total = 0;
static uint32_t diag_take_pending_total = 0;

/* Limite seguro para contadores - evita overflow com margem */
#define COUNTER_MAX_SAFE (UINT32_MAX - 10000)

/**
 * @brief Soma saturante: retorna a + b, ou COUNTER_MAX_SAFE se overflow.
 */
static inline uint32_t saturating_add(uint32_t a, uint32_t b) {
  if (a > COUNTER_MAX_SAFE - b) {
    return COUNTER_MAX_SAFE;
  }
  return a + b;
}

/**
 * @brief Estrutura centralizada para estado compartilhado entre cores.
 *
 * Esta estrutura armazena todos os dados voláteis que precisam ser
 * acessados ou modificados por diferentes tarefas ou núcleos.
 */
typedef struct {
  uint32_t pending_clicks; /**< Contador de cliques do botão A aguardando
                              processamento. */
  uint32_t
      syncing_count; /**< Snapshot de cliques durante sincronização offline. */
  uint32_t
      offline_clicks; /**< Cliques acumulados em modo offline (feedback). */
  uint32_t pending_turbo_activations; /**< Contador de pedidos de ativação de
                                         turbo do botão B. */
  uint32_t local_score;               /**< Pontuação local do dispositivo. */
  uint32_t global_score;              /**< Pontuação global da rede. */
  uint32_t node_scores[MAX_NODES]; /**< Array de pontuações individuais de todos
                                      os nós. */
  uint32_t turbo_until_ms;         /**< Timestamp de expiração do modo turbo. */
  connection_status_t
      connection_status;    /**< Estado atual da conexão WiFi/RPC. */
  bool milestone_triggered; /**< Flag indicando que um marco foi atingido. */
  bool led_flash_requested; /**< Flag solicitando feedback visual rápido
                               (flash). */
  bool turbo_active;    /**< Indica se o modo turbo está atualmente em vigor. */
  bool fallback_in_use; /**< Indica se o IP de fallback está sendo utilizado. */
  bool server_error_active; /**< Indica se o servidor reportou erro persistente.
                             */
} shared_state_t;

/** @brief Instância privada do estado. */
static shared_state_t state;

/** @brief Ponteiro para a instância do spinlock de hardware. */
static spin_lock_t *state_lock;

void shared_state_init(void) {
  // Inicializa a estrutura com zeros
  memset(&state, 0, sizeof(shared_state_t)); // syncing_count inicia em 0
  state.connection_status = STATUS_CONNECTING;
  state.fallback_in_use = false;
  state.server_error_active = false;

  // Aloca um spinlock de hardware disponível
  int lock_num = spin_lock_claim_unused(true);
  state_lock = spin_lock_instance(lock_num);
}

/**
 * @brief Macro para adquirir o lock e desabilitar interrupções locais.
 * @note Armazena o estado anterior das interrupções na variável local
 * 'irq_status'.
 */
#define LOCK_STATE() uint32_t irq_status = spin_lock_blocking(state_lock)

/**
 * @brief Macro para liberar o lock e restaurar o estado das interrupções.
 */
#define UNLOCK_STATE() spin_unlock(state_lock, irq_status)

/* --- Implementação das Funções de Acesso --- */

uint32_t shared_state_get_pending_clicks(void) {
  LOCK_STATE();
  uint32_t val = state.pending_clicks;
  UNLOCK_STATE();
  return val;
}

void shared_state_increment_pending_clicks(void) {
  LOCK_STATE();
  state.pending_clicks = saturating_add(state.pending_clicks, 1);
  UNLOCK_STATE();
}

uint32_t shared_state_take_pending_clicks(void) {
  LOCK_STATE();
  uint32_t val = state.pending_clicks;
  state.pending_clicks = 0;
  UNLOCK_STATE();
  DIAG_CNT_INC(diag_take_pending_total);
  LOG_VERBOSE("[STATE]", "take_pending_clicks: val=%lu total_takes=%lu",
              (unsigned long)val, (unsigned long)diag_take_pending_total);
  return val;
}

uint32_t shared_state_get_syncing_count(void) {
  LOCK_STATE();
  uint32_t val = state.syncing_count;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_syncing_count(uint32_t count) {
  LOCK_STATE();
  uint32_t prev = state.syncing_count;
  state.syncing_count = count;
  UNLOCK_STATE();
  if (count != prev) {
    LOG_VERBOSE("[STATE]", "syncing_count: %lu -> %lu", (unsigned long)prev,
                (unsigned long)count);
  }
}

void shared_state_restore_clicks(uint32_t n) {
  LOCK_STATE();
  uint32_t prev = state.pending_clicks;
  state.pending_clicks = saturating_add(state.pending_clicks, n);
  uint32_t after = state.pending_clicks;
  UNLOCK_STATE();
  diag_restore_clicks_total += n;
  LOG_VERBOSE("[STATE]",
              "restore_clicks: n=%lu pending: %lu->%lu total_restored=%lu",
              (unsigned long)n, (unsigned long)prev, (unsigned long)after,
              (unsigned long)diag_restore_clicks_total);
}

void shared_state_add_offline_clicks(uint32_t n) {
  LOCK_STATE();
  state.offline_clicks += n;
  UNLOCK_STATE();
}

uint32_t shared_state_get_offline_clicks(void) {
  LOCK_STATE();
  uint32_t val = state.offline_clicks;
  UNLOCK_STATE();
  return val;
}

void shared_state_clear_offline_clicks(void) {
  LOCK_STATE();
  state.offline_clicks = 0;
  UNLOCK_STATE();
}

uint32_t shared_state_get_local_score(void) {
  LOCK_STATE();
  uint32_t val = state.local_score;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_local_score(uint32_t score) {
  LOCK_STATE();
  state.local_score = score;
  UNLOCK_STATE();
}

uint32_t shared_state_get_global_score(void) {
  LOCK_STATE();
  uint32_t val = state.global_score;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_global_score(uint32_t score) {
  LOCK_STATE();
  state.global_score = score;
  UNLOCK_STATE();
}

void shared_state_get_node_scores(uint32_t *out_scores, uint8_t count) {
  LOCK_STATE();
  uint8_t to_copy = (count < MAX_NODES) ? count : MAX_NODES;
  if (out_scores) {
    memcpy(out_scores, state.node_scores, to_copy * sizeof(uint32_t));
  }
  UNLOCK_STATE();
}

void shared_state_set_node_scores(const uint32_t *in_scores, uint8_t count) {
  LOCK_STATE();
  uint8_t to_copy = (count < MAX_NODES) ? count : MAX_NODES;
  if (in_scores) {
    memcpy(state.node_scores, in_scores, to_copy * sizeof(uint32_t));
  }
  UNLOCK_STATE();
}

/* --- Atualização composta --- */

void shared_state_set_scores(const RpcClickResult *result) {
  LOCK_STATE();
  uint32_t prev_local = state.local_score;
  uint32_t prev_global = state.global_score;

  /* Protege contra valores negativos vindos do servidor */
  state.local_score =
      (result->local_score >= 0) ? (uint32_t)result->local_score : 0;
  state.global_score =
      (result->global_score >= 0) ? (uint32_t)result->global_score : 0;

  /* Nota: connection_status NÃO é alterado aqui — apenas task_rpc deve
   * gerenciar transições de status para evitar flickering durante connecting,
   * syncing, e transições intermediárias. */

  if (result->milestone_triggered) {
    state.milestone_triggered = true;
  }
  UNLOCK_STATE();

  LOG_VERBOSE("[STATE]",
              "set_scores: local=%lu->%lu global=%lu->%lu milestone=%d",
              (unsigned long)prev_local, (unsigned long)result->local_score,
              (unsigned long)prev_global, (unsigned long)result->global_score,
              (int)result->milestone_triggered);
}

connection_status_t shared_state_get_connection_status(void) {
  LOCK_STATE();
  connection_status_t status = state.connection_status;
  UNLOCK_STATE();
  return status;
}

void shared_state_set_connection_status(connection_status_t status) {
  LOCK_STATE();
  connection_status_t prev = state.connection_status;
  state.connection_status = status;
  UNLOCK_STATE();
  if (prev != status) {
    DIAG_CNT_INC(diag_status_transitions);
    LOG_NORMAL("[STATE]", "STATUS: %s -> %s (trans#%lu)",
               conn_status_name((int)prev), conn_status_name((int)status),
               (unsigned long)diag_status_transitions);
  }
}

bool shared_state_get_milestone_triggered(void) {
  LOCK_STATE();
  bool val = state.milestone_triggered;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_milestone_triggered(bool triggered) {
  LOCK_STATE();
  state.milestone_triggered = triggered;
  UNLOCK_STATE();
}

bool shared_state_take_milestone_triggered(void) {
  LOCK_STATE();
  bool val = state.milestone_triggered;
  state.milestone_triggered = false;
  UNLOCK_STATE();
  return val;
}

bool shared_state_get_led_flash_requested(void) {
  LOCK_STATE();
  bool val = state.led_flash_requested;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_led_flash_requested(bool requested) {
  LOCK_STATE();
  state.led_flash_requested = requested;
  UNLOCK_STATE();
}

bool shared_state_take_led_flash_requested(void) {
  LOCK_STATE();
  bool val = state.led_flash_requested;
  state.led_flash_requested = false;
  UNLOCK_STATE();
  return val;
}

bool shared_state_get_fallback_in_use(void) {
  LOCK_STATE();
  bool val = state.fallback_in_use;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_fallback_in_use(bool in_use) {
  LOCK_STATE();
  state.fallback_in_use = in_use;
  UNLOCK_STATE();
}

bool shared_state_get_server_error_active(void) {
  LOCK_STATE();
  bool val = state.server_error_active;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_server_error_active(bool active) {
  LOCK_STATE();
  state.server_error_active = active;
  UNLOCK_STATE();
}

void shared_state_request_turbo_activation(void) {
  LOCK_STATE();
  state.pending_turbo_activations++;
  UNLOCK_STATE();
}

bool shared_state_take_turbo_activation_requested(void) {
  LOCK_STATE();
  bool requested = state.pending_turbo_activations > 0;
  if (requested) {
    state.pending_turbo_activations--;
  }
  UNLOCK_STATE();
  return requested;
}

bool shared_state_get_turbo_active(void) {
  LOCK_STATE();
  bool val = state.turbo_active;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_turbo_active(bool active) {
  LOCK_STATE();
  state.turbo_active = active;
  UNLOCK_STATE();
}

uint32_t shared_state_get_turbo_until_ms(void) {
  LOCK_STATE();
  uint32_t val = state.turbo_until_ms;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_turbo_until_ms(uint32_t until_ms) {
  LOCK_STATE();
  state.turbo_until_ms = until_ms;
  UNLOCK_STATE();
}

void shared_state_lock_enter(uint32_t *save) {
  *save = spin_lock_blocking(state_lock);
}

void shared_state_lock_exit(uint32_t save) { spin_unlock(state_lock, save); }

void shared_state_get_display_snapshot(display_snapshot_t *snapshot) {
  LOCK_STATE();
  snapshot->local_score = state.local_score;
  snapshot->global_score = state.global_score;
  snapshot->pending_clicks = state.pending_clicks;
  snapshot->syncing_count = state.syncing_count;
  snapshot->offline_clicks = state.offline_clicks;
  snapshot->status = state.connection_status;
  snapshot->turbo_active = state.turbo_active;
  snapshot->turbo_until_ms = state.turbo_until_ms;
  UNLOCK_STATE();
}
