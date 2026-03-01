#include "lamport.h"
#include "middleware/shared_state.h"
#include <stdio.h>

static uint32_t lamport_L = 0;

void lamport_init(void) {
  /* shared_state_init() deve ter sido chamado antes */
  uint32_t save;
  shared_state_lock_enter(&save);
  lamport_L = 0;
  shared_state_lock_exit(save);
  printf("[LAMPORT] Inicializado com ts=0\n");
}

uint32_t lamport_tick(void) {
  uint32_t save;
  shared_state_lock_enter(&save);
  lamport_L += 1u;
  uint32_t current = lamport_L;
  shared_state_lock_exit(save);

  printf("[LAMPORT] Tick: ts=%lu\n", (unsigned long)current);
  return current;
}

void lamport_update(uint32_t received_ts) {
  uint32_t save;
  shared_state_lock_enter(&save);
  if (received_ts > lamport_L) {
    lamport_L = received_ts;
  }
  lamport_L += 1u;
  uint32_t current = lamport_L;
  shared_state_lock_exit(save);

  printf("[LAMPORT] Atualizado para ts=%lu (recebido=%lu)\n",
         (unsigned long)current, (unsigned long)received_ts);
}

uint32_t lamport_get_current(void) {
  uint32_t save;
  shared_state_lock_enter(&save);
  uint32_t current = lamport_L;
  shared_state_lock_exit(save);
  return current;
}
