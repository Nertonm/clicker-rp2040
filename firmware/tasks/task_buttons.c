/**
 * @file task_buttons.c
 * @author
 * @date 2026-03-01
 * @brief Implementação da tarefa de processamento de cliques.
 *
 * Esta tarefa detecta novos cliques registrados pela ISR, emite feedback
 * sonoro via buzzer e solicita feedback visual via LED flash.
 */

#include "FreeRTOS.h"
#include "task.h"

#include "audio/buzzer.h"
#include "drivers/display/ws2812.h"
#include "middleware/app_queues.h"
#include "middleware/game_events.h"
#include "middleware/shared_state.h"
#include "task_buttons.h"
#include <stdio.h>

/** @brief Frequência do beep de feedback de clique (Hz). */
#define CLICK_BEEP_FREQ_HZ 1200u
/** @brief Duração do beep de feedback de clique (ms). */
#define CLICK_BEEP_DURATION_MS 25u

/**
 * @brief Loop principal da tarefa de botões.
 *
 * @param[in] param Parâmetro opcional da tarefa (não utilizado).
 */
void task_buttons(void *param) {
  (void)param;

  uint32_t prev_pending =
      0; /* Rastreia valor anterior para detectar novos cliques */

  while (1) {
    /* Lê (sem consumir) quantos cliques estão pendentes para feedback de LED.
     * task_rpc é o único consumidor legítimo (via take_pending_clicks).
     * Usar take() aqui criava uma race window onde task_rpc lia 0 cliques. */
    uint32_t pending = shared_state_get_pending_clicks();

    /* Detecta novos cliques: o contador cresceu desde o último ciclo */
    if (pending > prev_pending) {
      /* Feedback visual: flash de LED */
      shared_state_set_led_flash_requested(true);

      /* Feedback sonoro: beep curto não-bloqueante via PWM */
      buzzer_tone(CLICK_BEEP_FREQ_HZ, CLICK_BEEP_DURATION_MS);
    }

    /* Salva valor atual para detectar novos cliques no próximo ciclo.
     * Se task_rpc consumiu (pending caiu), prev_pending se ajusta
     * automaticamente — o buzzer não dispara quando clicks são enviados. */
    prev_pending = pending;

    /* Intervalo de poll da tarefa (20ms) */
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
