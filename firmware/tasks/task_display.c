/**
 * @file task_display.c
 * @author
 * @date 2026-03-01
 * @brief Implementação da tarefa de atualização da interface visual.
 *
 * Gerencia a renderização periódica do display OLED e o controle de cores
 * da matriz de LEDs WS2812, processando feedbacks visuais para turbo,
 * cliques e marcos de pontuação.
 */

#include "task_display.h"
#include "FreeRTOS.h"
#include "config/firmware_config.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware_config.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"
#include "task.h"
#include <stdio.h>

/**
 * @brief Calcula cores do arco-íris para efeitos visuais (roda de cores).
 *
 * @param[in] pos Posição na roda de cores (0-255).
 * @param[out] r Componente Vermelho.
 * @param[out] g Componente Verde.
 * @param[out] b Componente Azul.
 */
static void wheel_color(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t p = 255 - pos;
  if (p < 85) {
    *r = 255 - p * 3;
    *g = 0;
    *b = p * 3;
  } else if (p < 170) {
    p -= 85;
    *r = 0;
    *g = p * 3;
    *b = 255 - p * 3;
  } else {
    p -= 170;
    *r = p * 3;
    *g = 255 - p * 3;
    *b = 0;
  }
}

/**
 * @brief Formata número com separador de milhar.
 */
static void format_number(char *buf, size_t size, uint32_t value) {
  if (value >= 1000000) {
    snprintf(buf, size, "%lu,%03lu,%03lu", (unsigned long)(value / 1000000),
             (unsigned long)((value / 1000) % 1000),
             (unsigned long)(value % 1000));
  } else if (value >= 1000) {
    snprintf(buf, size, "%lu,%03lu", (unsigned long)(value / 1000),
             (unsigned long)(value % 1000));
  } else {
    snprintf(buf, size, "%lu", (unsigned long)value);
  }
}

/**
 * @brief Retorna caractere de indicador de status.
 */
static char status_indicator(connection_status_t status) {
  switch (status) {
  case STATUS_ONLINE:
    return '*';
  case STATUS_OFFLINE:
    return '!';
  case STATUS_CONNECTING:
    return '~';
  case STATUS_SYNCING:
    return '>';
  default:
    return '?';
  }
}

/**
 * @brief Loop principal da tarefa de display.
 */
void task_display(void *param) {
  (void)param;

  char line[24];
  char score_buf[16];
  uint8_t rainbow_phase = 0;
  uint8_t milestone_glow_ticks = 0;
  uint32_t turbo_remaining_ms = 0;

  while (1) {
    /* Leitura atômica do estado compartilhado (snapshot consistente) */
    display_snapshot_t snap;
    shared_state_get_display_snapshot(&snap);

    uint32_t local = snap.local_score;
    uint32_t global = snap.global_score;
    connection_status_t status = snap.status;
    bool turbo_active = snap.turbo_active;

    /* Flags consumíveis (take) continuam separadas */
    bool led_flash = shared_state_take_led_flash_requested();
    bool milestone = shared_state_take_milestone_triggered();

    /* Gerenciamento do tempo de expiração do modo turbo */
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (turbo_active) {
      uint32_t until_ms = snap.turbo_until_ms;
      int32_t remaining = (int32_t)(until_ms - now_ms);
      if (remaining <= 0) {
        shared_state_set_turbo_active(false);
        turbo_active = false;
        turbo_remaining_ms = 0;
      } else {
        turbo_remaining_ms = (uint32_t)remaining;
      }
    }

    /* Feedback visual de Marco (Milestone) atingido */
    if (milestone) {
      milestone_glow_ticks = MILESTONE_GLOW_TICKS;
      /* Efeito de flash imediato nos LEDs */
      for (int i = 0; i < 2; i++) {
        led_matrix_set_all(24, 16, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
        led_clear_all();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }

    /* Em modo offline/connecting/syncing, soma cliques pendentes para feedback
     */
    uint32_t local_display = local;
    uint32_t global_display = global;
    if (status == STATUS_OFFLINE || status == STATUS_CONNECTING ||
        status == STATUS_SYNCING) {
      local_display = local + snap.pending_clicks;
      global_display = global + snap.pending_clicks;
    }

    /* === RENDERIZAÇÃO DO DISPLAY === */
    display_clear();

    /* Linha 0: Título + indicador de status */
    snprintf(line, sizeof(line), "COOKIE CLICKER   [%c]",
             status_indicator(status));
    display_text(0, 0, line);

    /* Linha 1: Separador */
    display_text(1, 0, "--------------------");

    /* Linha 2: Score do nó local */
    format_number(score_buf, sizeof(score_buf), local_display);
    snprintf(line, sizeof(line), "Node %d:  %s", NODE_ID, score_buf);
    display_text(2, 0, line);

    /* Linha 3: Score global */
    format_number(score_buf, sizeof(score_buf), global_display);
    snprintf(line, sizeof(line), "Global:  %s", score_buf);
    display_text(3, 0, line);

    /* Linha 4: Vazia */
    display_text(4, 0, "");

    /* Linha 5: Separador */
    display_text(5, 0, "--------------------");

    /* Linha 6: Pending clicks (a enviar) */
    uint32_t pending_total = snap.pending_clicks;
    if (status == STATUS_SYNCING) {
      snprintf(line, sizeof(line), "Enviando: %lu",
               (unsigned long)snap.syncing_count);
    } else if (pending_total > 0) {
      snprintf(line, sizeof(line), "Pendente: %lu",
               (unsigned long)pending_total);
    } else {
      snprintf(line, sizeof(line), "Pendente: 0");
    }
    display_text(6, 0, line);

    /* Linha 7: Status ou Turbo */
    if (turbo_active) {
      uint32_t turbo_secs = turbo_remaining_ms / 1000;
      uint8_t bar_fill =
          (uint8_t)((turbo_remaining_ms * 10) / TURBO_DURATION_MS);
      if (bar_fill > 10)
        bar_fill = 10;
      char bar[12] = "..........";
      for (uint8_t i = 0; i < bar_fill; i++)
        bar[i] = '#';
      snprintf(line, sizeof(line), "TURBO[%s]%lus", bar,
               (unsigned long)turbo_secs);
      display_text(7, 0, line);
    } else if (status == STATUS_OFFLINE) {
      display_text(7, 0, "!! OFFLINE");
    } else if (status == STATUS_CONNECTING) {
      display_text(7, 0, "~~ Conectando...");
    } else {
      display_text(7, 0, "");
    }

    display_show();

    /* Atualização da Matriz de LEDs WS2812 (Dígito do score local) */
    uint8_t digit = (uint8_t)(local_display % 10u);

    if (milestone_glow_ticks > 0) {
      // Brilho dourado para marcos
      led_matrix_draw_number(digit, 20, 14, 0);
      milestone_glow_ticks--;
    } else if (turbo_active) {
      // Efeito arco-íris para modo turbo
      uint8_t r, g, b;
      wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
      led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                             (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      rainbow_phase += 7;
    } else {
      // Cor cinza padrão
      led_matrix_draw_number(digit, 8, 8, 8);
    }

    /* Indicador de modo OFFLINE nos LEDs */
    if (status == STATUS_OFFLINE && !turbo_active &&
        milestone_glow_ticks == 0 && !led_flash) {
      // Pisca LED central em vermelho fraco (alerta visual discreto)
      static uint32_t offline_frame_count = 0;
      offline_frame_count++;

      if ((offline_frame_count % 10) < 3) {
        led_set(12, 8, 0, 0); // LED central em vermelho
      }
    }

    /* Feedback visual de clique (flash azul central) */
    if (led_flash) {
      led_set(12, 0, 0, 15);
      vTaskDelay(pdMS_TO_TICKS(50));
      // Restaura o número após o flash
      if (milestone_glow_ticks > 0) {
        led_matrix_draw_number(digit, 20, 14, 0);
      } else if (turbo_active) {
        uint8_t r, g, b;
        wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
        led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                               (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      } else {
        led_matrix_draw_number(digit, 8, 8, 8);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}
