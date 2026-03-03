/**
 * @file core1_display.c
 * @brief Implementação da lógica de exibição dedicada para o Core 1.
 *
 * Realiza o polling do estado compartilhado e atualiza os periféricos visuais
 * de forma independente do processamento principal no Core 0.
 */

#include "core1_display.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "pico/stdlib.h"
#include "shared_state_reader.h"
#include <stdio.h>

/** @brief Intervalo de atualização do loop do Core 1 (100ms). */
#define DISPLAY_REFRESH_MS 100

void core1_display_entry(void) {
  // Inicializa o hardware da matriz de LEDs no Core 1 (Dono exclusivo do
  // recurso)
  led_matrix_init();

  while (true) {
    /* Coleta de dados do estado (Apenas leitura) */
    uint32_t score = shared_state_get_local_score();
    connection_status_t status = shared_state_get_connection_status();
    bool milestone = shared_state_take_milestone_triggered();
    bool led_flash = shared_state_take_led_flash_requested();
    bool fallback = shared_state_get_fallback_in_use();
    bool server_error = shared_state_get_server_error_active();

    /* Em modo offline/syncing/connecting, soma pending_clicks ao score
     * para feedback visual imediato — matriz não fica em zero quando offline.
     */
    char buf_score[20];
    char buf_status[20];
    uint32_t pending = shared_state_get_pending_clicks();
    uint32_t display_score = score;
    if (status == STATUS_OFFLINE || status == STATUS_CONNECTING ||
        status == STATUS_SYNCING) {
      display_score = score + pending;
    }

    // Formata placar com cliques pendentes incluídos quando offline
    snprintf(buf_score, sizeof(buf_score), "Placar: %u",
             (unsigned int)display_score);

    // Formata status de conexão para exibição
    switch (status) {
    case STATUS_ONLINE:
      if (server_error) {
        snprintf(buf_status, sizeof(buf_status), "SERVER FAIL");
      } else if (fallback) {
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

    /* Tratamento de Marcos (Milestones) */
    if (milestone) {
      display_text(6, 0, "MILESTONE!");
      // Blink duplo dourado em toda a matriz
      for (int j = 0; j < 2; j++) {
        led_matrix_set_all(15, 10, 0);
        sleep_ms(150);
        led_clear_all();
        sleep_ms(100);
      }
      // Mostra o número 0 em dourado como destaque
      led_matrix_draw_number(0, 15, 10, 0);
    } else {
      // Atualiza a matriz de LEDs com o dígito do score (inclui pending
      // offline)
      uint8_t digito = (uint8_t)(display_score % 10u);
      led_matrix_draw_number(digito, 8, 8, 8);
    }

    display_show();

    /* Feedback visual de clique (Flash azul no LED central) */
    bool just_flashed = false;
    if (led_flash) {
      led_set(12, 0, 0, 15);
      sleep_ms(50);
      led_set(12, 0, 0, 0);
      just_flashed = true;
    }

    /* Lógica de Heartbeat (Sinal de vida do sistema) */
    static uint32_t last_blink = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_blink > 1000) {
      last_blink = now;
      static bool hb_state = false;
      hb_state = !hb_state;

      // Só aplica o estado do heartbeat se não houver conflito com o flash de
      // clique
      if (!just_flashed) {
        if (hb_state) {
          if (server_error) {
            led_set(12, 15, 0, 0); // Vermelho: Erro de servidor/Discovery
          } else {
            led_set(12, 4, 4, 4); // Branco suave: Operação normal
          }
        } else {
          led_set(12, 0, 0, 0);
        }
      }
    }

    sleep_ms(DISPLAY_REFRESH_MS);
  }
}
