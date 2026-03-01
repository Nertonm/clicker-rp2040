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
#include "task.h"
#include "config/firmware_config.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware_config.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"
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
 * @brief Converte o status da conexão para uma string legível.
 *
 * @param[in] status Estado da conexão RPC/WiFi.
 * @return const char* String correspondente ao estado.
 */
static const char *status_to_text(connection_status_t status) {
  switch (status) {
  case STATUS_ONLINE:
    return "ONLINE";
  case STATUS_OFFLINE:
    return "OFFLINE";
  case STATUS_CONNECTING:
    return "CONNECTING";
  case STATUS_SYNCING:
    return "SYNCING";
  default:
    return "UNKNOWN";
  }
}

/**
 * @brief Loop principal da tarefa de display.
 */
void task_display(void *param) {
  (void)param;

  char row1[24];
  char row2[24];
  char row3[24];
  uint8_t rainbow_phase = 0;
  uint8_t milestone_glow_ticks = 0;

  while (1) {
    /* Leitura atômica do estado compartilhado */
    uint32_t local = shared_state_get_local_score();
    uint32_t global = shared_state_get_global_score();
    connection_status_t status = shared_state_get_connection_status();
    uint32_t pending = shared_state_get_pending_clicks();
    bool led_flash = shared_state_take_led_flash_requested();
    bool milestone = shared_state_take_milestone_triggered();
    bool turbo_active = shared_state_get_turbo_active();

    /* Gerenciamento do tempo de expiração do modo turbo */
    if (turbo_active) {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      uint32_t until_ms = shared_state_get_turbo_until_ms();
      if ((int32_t)(until_ms - now_ms) <= 0) {
        shared_state_set_turbo_active(false);
        turbo_active = false;
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

    /* Preparação das linhas de texto para o OLED */
    snprintf(row1, sizeof(row1), "Node %d: %lu", NODE_ID, (unsigned long)local);
    snprintf(row2, sizeof(row2), "Global: %lu", (unsigned long)global);

    if (turbo_active) {
      snprintf(row3, sizeof(row3), "TURBO x3");
    } else if (status == STATUS_SYNCING) {
      // [>] = símbolo de "enviando" (Unicode ↻ não funciona no SSD1306)
      snprintf(row3, sizeof(row3), "[>] SYNCING P:%lu", (unsigned long)pending);
    } else if (status == STATUS_OFFLINE) {
      // Formato compacto para display de 128x64 pixels (aprox. 21 chars/linha)
      snprintf(row3, sizeof(row3), "[o] OFFLINE P:%lu", (unsigned long)pending);
    } else {
      snprintf(row3, sizeof(row3), "%s", status_to_text(status));
    }

    /* Atualização do Display SSD1306 */
    display_clear();
    display_text(0, 0, "Cookie Clicker");
    display_text(2, 0, row1);
    display_text(4, 0, row2);
    display_text(6, 0, row3);
    display_show();

    /* Atualização da Matriz de LEDs WS2812 (Dígito menos significativo do score local) */
    uint8_t digit = (uint8_t)(local % 10u);
    
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
    if (status == STATUS_OFFLINE && !turbo_active && milestone_glow_ticks == 0 &&
        !led_flash) {
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
