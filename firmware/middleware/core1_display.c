#include "core1_display.h"
#include "drivers/display/display.h"
#include "pico/stdlib.h"
#include "shared_state_reader.h" // só getters — sem acesso a setters
#include <stdio.h>

#define DISPLAY_REFRESH_MS 100

void core1_display_entry(void) {
  // Core 1 inicializa seu próprio contexto — display já foi inicializado
  // pelo Core 0 no boot, não chame display_init() aqui.

  while (true) {
    uint32_t score = shared_state_get_local_score();
    connection_status_t status = shared_state_get_connection_status();

    char buf_score[20];
    char buf_status[20];

    // Formata placar
    snprintf(buf_score, sizeof(buf_score), "Placar: %u", score);

    // Formata status de conexão
    switch (status) {
    case STATUS_ONLINE:
      snprintf(buf_status, sizeof(buf_status), "Online");
      break;
    case STATUS_OFFLINE:
      snprintf(buf_status, sizeof(buf_status), "Offline");
      break;
    case STATUS_CONNECTING:
      snprintf(buf_status, sizeof(buf_status), "Conectando");
      break;
    case STATUS_SYNCING:
      snprintf(buf_status, sizeof(buf_status), "Sincroniz.");
      break;
    default:
      snprintf(buf_status, sizeof(buf_status), "?");
      break;
    }

    display_clear();
    display_text(0, 0, "Cookie Clicker");
    display_text(2, 0, buf_score);
    display_text(4, 0, buf_status);
    display_show();

    sleep_ms(DISPLAY_REFRESH_MS);
  }
}
