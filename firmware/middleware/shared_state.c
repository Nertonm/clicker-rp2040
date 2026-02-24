#include "shared_state.h"
// #include "pico/multicore.h" // Não utilizado diretamente
// #include "pico/stdlib.h" // Não utilizado diretamente
// fornce spin_lock_t blocking unlock claim_unsed lock_instace
#include "hardware/sync.h"

typedef struct {
  // Protegido por __atomic operação indivisel
  uint32_t pending_clicks;
  uint32_t connection_status;
  bool debug_mode;

  // Protegido por spin_lock_t
  // multiplos campos lidos pelo core 1
  uint32_t global_score;
  uint32_t local_store;
  uint32_t node_scores[MAX_NODES];
} shared_state_t;

// Static para garantir que só vai acessar atravaes daqui
static shared_state_t state;
static spin_lock_t *scores_lock;

void share_state_init(void) {
  // Zera tudo e seta o status inicial
  state = (shared_state_t){0};
  state.connection_status = STATUS_CONNECTING;

  int lock_num = spin_lock_claim_unused(true);
  scores_lock = spin_lock_instance(lock_num);
}

// IRQ botão A
void shared_state_increment_clicks(void) {
  // Incrementa de forma atômica para evitar condições de corrida
  __atomic_fetch_add(&state.pending_clicks, 1, __ATOMIC_ACQ_REL);
}

void shared_state_toggle_debug_mode(void) {
  // O atomic_store garante que ele veja o valor correto
  bool current = __atomic_load_n(&state.debug_mode, __ATOMIC_ACQUIRE);
  __atomic_store_n(&state.debug_mode, !current, __ATOMIC_RELEASE);
}

// Loop Core 0 - Escrita
uint32_t shared_state_take_clicks(void) {
  // Zera os cliques pendentes e retorna o valor antigo de forma atômica
  return __atomic_exchange_n(&state.pending_clicks, 0, __ATOMIC_ACQ_REL);
}

// Se falhar retorna os cliques e soma com oq já tem, para não perder nenhum
// clique
void shared_state_restore_clicks(uint32_t clicks) {
  __atomic_fetch_add(&state.pending_clicks, clicks, __ATOMIC_ACQ_REL);
}

void shared_state_set_status(connection_status_t status) {
  __atomic_store_n(&state.connection_status, (uint32_t)status,
                   __ATOMIC_RELEASE);
}

void shared_state_set_scores(uint32_t global, uint32_t local,
                             uint32_t *node_scores, uint8_t count) {
  uint32_t irq = spin_lock_blocking(scores_lock);
  state.global_score = global;
  state.local_store = local;
  for (uint8_t i = 0; i < count && i < MAX_NODES; i++) {
    state.node_scores[i] = node_scores[i];
  }
  spin_unlock(scores_lock, irq);
}

// Loop Core 0 -  Leitura

// Loop Apresentação Core 1 - Leitura
connection_status_t shared_state_get_status(void) {
  return (connection_status_t)__atomic_load_n(&state.connection_status,
                                              __ATOMIC_ACQUIRE);
}

bool shared_state_get_debug_mode(void) {
  return __atomic_load_n(&state.debug_mode, __ATOMIC_ACQUIRE);
}

void shared_state_get_scores(uint32_t *out_global, uint32_t *out_local,
                             uint32_t *out_nodes, uint8_t count) {
  uint32_t irq = spin_lock_blocking(scores_lock);
  if (out_global)
    *out_global = state.global_score;
  if (out_local)
    *out_local = state.local_store;
  for (uint8_t i = 0; i < count && i < MAX_NODES; i++) {
    if (out_nodes)
      out_nodes[i] = state.node_scores[i];
  }
  spin_unlock(scores_lock, irq);
}
