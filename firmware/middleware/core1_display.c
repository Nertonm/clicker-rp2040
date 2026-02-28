#include "core1_display.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "pico/stdlib.h"
#include "shared_state_reader.h" // só getters - sem acesso a setters
#include <stdio.h>

#define DISPLAY_REFRESH_MS 100

void core1_display_entry(void) {
  // Inicializa o hardware da matriz de LEDs no Core 1 (Dono exclusivo do
  // recurso)
  led_matrix_init();

  // Core 1 inicializa seu próprio contexto - display já foi inicializado
  // pelo Core 0 no boot, não chame display_init() aqui.

  while (true) {
    uint32_t score = shared_state_get_local_score();
    connection_status_t status = shared_state_get_connection_status();
    bool milestone = shared_state_take_milestone_triggered();
    bool led_flash = shared_state_take_led_flash_requested();

    bool fallback = shared_state_get_fallback_in_use();

    char buf_score[20];
    char buf_status[20];

    // Formata placar
    snprintf(buf_score, sizeof(buf_score), "Placar: %u", score);

    // Formata status de conexão
    switch (status) {
    case STATUS_ONLINE:
      if (fallback) {
        snprintf(buf_status, sizeof(buf_status), "FALLBACK IP");
      } else {
        snprintf(buf_status, sizeof(buf_status), "WIFI OK");
      }
      break;
    case STATUS_OFFLINE:
      snprintf(buf_status, sizeof(buf_status), "WIFI FAIL");
      break;
    case STATUS_CONNECTING:
      snprintf(buf_status, sizeof(buf_status), "CONNECTING...");
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

    // Milestone: mostra indicador visual no display
    if (milestone) {
      display_text(6, 0, "MILESTONE!");
      // Blink duplo dourado em toda a matriz (suave via driver)
      for (int j = 0; j < 2; j++) {
        led_matrix_set_all(15, 10, 0); // Dourado escuro agradável
        sleep_ms(150);
        led_clear_all();
        sleep_ms(100);
      }
      // Mostra o número 0 em dourado
      led_matrix_draw_number(0, 15, 10, 0);
    } else {
      // Atualiza a matriz de LEDs com o dígito atual (Apresentação Core 1)
      uint8_t digito = score % 10;
      led_matrix_draw_number(digito, 8, 8, 8); // Branco suave e perceptível
    }

    display_show();

    // LED flash: pisca LED central se solicitado (Feedback de clique)
    bool just_flashed = false;
    if (led_flash) {
      led_set(12, 0, 0, 15); // Feedback azulado fraco
      sleep_ms(50);
      led_set(12, 0, 0, 0);
      just_flashed = true;
    }

    // Heartbeat: pisca o LED central a cada ~1s para indicar "estamos vivos"
    static uint32_t last_blink = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_blink > 1000) {
      last_blink = now;
      static bool hb_state = false;
      hb_state = !hb_state;

      // Só aplica o estado do heartbeat se não tivermos acabado de piscar pelo
      // clique Isso evita que o heartbeat apague o feedback do clique
      // imediatamente.
      if (!just_flashed) {
        if (hb_state) {
          led_set(12, 4, 4, 4); // Branco de fundo/heartbeat
        } else {
          led_set(12, 0, 0, 0);
        }
      }
    }

    sleep_ms(DISPLAY_REFRESH_MS);
  }
}
