#include "shared_state.h"
#include "hardware/sync.h"
#include <string.h>

/**
 * @brief Estrutura centralizada para estado compartilhado entre cores.
 */
typedef struct {
  uint32_t pending_clicks;
  uint32_t local_score;
  uint32_t global_score;
  uint32_t node_scores[MAX_NODES];
  connection_status_t connection_status;
  uint32_t current_lamport_ts;
  bool milestone_triggered;
  bool led_flash_requested;
} shared_state_t;

static shared_state_t state;
static spin_lock_t *state_lock;

void shared_state_init(void) {
  // Inicializa a estrutura com zeros
  memset(&state, 0, sizeof(shared_state_t));
  state.connection_status = STATUS_CONNECTING;

  // Aloca um spinlock de hardware
  int lock_num = spin_lock_claim_unused(true);
  state_lock = spin_lock_instance(lock_num);
}

// Macros auxiliares para reduzir repetição de código com spinlock
#define LOCK_STATE() uint32_t irq_status = spin_lock_blocking(state_lock)
#define UNLOCK_STATE() spin_unlock(state_lock, irq_status)

uint32_t shared_state_get_pending_clicks(void) {
  LOCK_STATE();
  uint32_t val = state.pending_clicks;
  UNLOCK_STATE();
  return val;
}

void shared_state_increment_pending_clicks(void) {
  LOCK_STATE();
  state.pending_clicks++;
  UNLOCK_STATE();
}

uint32_t shared_state_take_pending_clicks(void) {
  LOCK_STATE();
  uint32_t val = state.pending_clicks;
  state.pending_clicks = 0;
  UNLOCK_STATE();
  return val;
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

connection_status_t shared_state_get_connection_status(void) {
  LOCK_STATE();
  connection_status_t status = state.connection_status;
  UNLOCK_STATE();
  return status;
}

void shared_state_set_connection_status(connection_status_t status) {
  LOCK_STATE();
  state.connection_status = status;
  UNLOCK_STATE();
}

uint32_t shared_state_get_lamport_ts(void) {
  LOCK_STATE();
  uint32_t val = state.current_lamport_ts;
  UNLOCK_STATE();
  return val;
}

void shared_state_set_lamport_ts(uint32_t ts) {
  LOCK_STATE();
  state.current_lamport_ts = ts;
  UNLOCK_STATE();
}

void shared_state_increment_lamport_ts(void) {
  LOCK_STATE();
  state.current_lamport_ts++;
  UNLOCK_STATE();
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
