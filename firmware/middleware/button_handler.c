/**
 * @file button_handler.c
 * @brief Implementação do tratamento de botões físicos e interrupções GPIO.
 *
 * Gerencia a detecção de borda de descida nos pinos dos botões, aplica
 * lógica de debounce baseada em tempo e atualiza os contadores no estado compartilhado.
 *
 * @author
 * @date 2026-03-01
 */

#include "button_handler.h"
#include <stdio.h>

#include "hardware_config.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "shared_state.h"

/** @name Variáveis de Controle de Debouncing */
/** @{ */
static volatile uint64_t last_a_us = 0; /**< Último timestamp de pressionamento válido do botão A. */
static volatile uint64_t last_b_us = 0; /**< Último timestamp de pressionamento válido do botão B. */
/** @} */

uint32_t button_handler_get_a_count(void) {
  return shared_state_get_pending_clicks();
}

uint32_t button_handler_get_b_count(void) {
  /** 
   * @note Atualmente, o botão B solicita ativação de turbo em vez de incrementar um contador.
   * Retorna 0 por compatibilidade com a interface definida no header.
   */
  return 0;
}

/**
 * @brief Callback de interrupção (ISR) para eventos GPIO.
 *
 * Processa as interrupções de todos os botões configurados. Realiza o
 * debounce comparando o tempo atual com o último evento registrado.
 *
 * @param[in] gpio Número do pino GPIO que gerou a interrupção.
 * @param[in] events Máscara de eventos que ocorreram (ex: borda de descida).
 */
static void gpio_irq_callback(uint gpio, uint32_t events) {
  uint64_t now = time_us_64();

  /* Processamento do Botão A (Cliques) */
  if (gpio == BUTTON1_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_a_us >= DEBOUNCE_US) {
      last_a_us = now;
      shared_state_increment_pending_clicks();
    }
  } 
  /* Processamento do Botão B (Turbo) */
  else if (gpio == BUTTON2_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_b_us >= DEBOUNCE_US) {
      last_b_us = now;
      shared_state_request_turbo_activation();
    }
  }
}

void button_handler_init(void) {
  /* Configuração do Botão A */
  gpio_init(BUTTON1_PIN);
  gpio_set_dir(BUTTON1_PIN, GPIO_IN);
  gpio_pull_up(BUTTON1_PIN);

  /* Configuração do Botão B */
  gpio_init(BUTTON2_PIN);
  gpio_set_dir(BUTTON2_PIN, GPIO_IN);
  gpio_pull_up(BUTTON2_PIN);

  /* Registro do callback único e habilitação das interrupções no Core 0 */
  gpio_set_irq_enabled_with_callback(BUTTON1_PIN, GPIO_IRQ_EDGE_FALL, true,
                                     &gpio_irq_callback);
  gpio_set_irq_enabled(BUTTON2_PIN, GPIO_IRQ_EDGE_FALL, true);
}
