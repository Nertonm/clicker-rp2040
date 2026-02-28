#ifndef SHARED_STATE_READER_H
#define SHARED_STATE_READER_H

#include "shared_state.h"
#include <stdbool.h>
#include <stdint.h>

/* ── Somente leitura — uso exclusivo do Core 1 ──
 * Core 1 inclui ESTE header, nunca shared_state.h diretamente.
 * Os setters (shared_state_set_*) são invisíveis daqui. */

uint32_t shared_state_get_local_score(void);
uint32_t shared_state_get_global_score(void);
void shared_state_get_node_scores(uint32_t *out, uint8_t count);
connection_status_t shared_state_get_connection_status(void);

// Flags de Eventos — take lê e zera atomicamente (uso exclusivo Core 1)
bool shared_state_take_milestone_triggered(void);
bool shared_state_take_led_flash_requested(void);

#endif // SHARED_STATE_READER_H
