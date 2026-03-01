#include "lamport.h"
#include "pico/critical_section.h"
#include <stdio.h>

// Estado interno
static int lamport_clock = 0;
static critical_section_t lamport_lock;

void lamport_init(void) {
  critical_section_init(&lamport_lock);
  lamport_clock = 0;
  printf("[LAMPORT] Inicializado com ts=0\n");
}

int lamport_tick(void) {
  critical_section_enter_blocking(&lamport_lock);
  lamport_clock++;
  int current = lamport_clock;
  critical_section_exit(&lamport_lock);

  printf("[LAMPORT] Tick: ts=%d\n", current);
  return current;
}

void lamport_update(int received_ts) {
  critical_section_enter_blocking(&lamport_lock);

  // Servidor é autoritativo - ele já calculou max(local, received) + 1
  // Cliente apenas aceita esse valor sincronizado
  lamport_clock = received_ts;

  int current = lamport_clock;
  critical_section_exit(&lamport_lock);

  printf("[LAMPORT] Atualizado para ts=%d (servidor)\n", current);
}

int lamport_get(void) {
  critical_section_enter_blocking(&lamport_lock);
  int current = lamport_clock;
  critical_section_exit(&lamport_lock);

  return current;
}
