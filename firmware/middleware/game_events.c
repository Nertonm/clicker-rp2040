/**
 * @file game_events.c
 * @author
 * @date 2026-03-01
 * @brief Implementação da lógica de eventos globais do jogo.
 *
 * Centraliza o processamento de gatilhos (triggers) que resultam em
 * múltiplas ações coordenadas (áudio + estado visual).
 */

#include "game_events.h"
#include "audio/buzzer.h"
#include "middleware/shared_state.h"
#include "pico/stdlib.h"

/**
 * @brief Dispara o feedback visual e sonoro para marcos de pontuação atingidos.
 *
 * Realiza o controle de intervalo (throttle) para evitar disparos excessivos
 * em um curto período de tempo e ativa o buzzer e flags de estado.
 *
 * @note Possui proteção de reentrada temporal de 250ms.
 */
void trigger_milestone_feedback(void) {
  static uint32_t last_trigger_ms = 0;
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());

  // Evita disparos múltiplos muito rápidos (limite de 250ms)
  if ((int32_t)(now_ms - last_trigger_ms) < 250) {
    return;
  }

  last_trigger_ms = now_ms;
  
  /* Sinaliza ao Core 1 (Tarefa de Display) que um marco ocorreu */
  shared_state_set_milestone_triggered(true);
  
  /* Emite um tom agudo no buzzer */
  buzzer_tone(1800, 120);
}
