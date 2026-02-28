#include "button_handler.h"
#include <stdio.h>

#include "hardware_config.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

#include "shared_state.h"

// Variáveis para debouncing
static volatile uint64_t last_a_us = 0;
static volatile uint64_t last_b_us = 0;

// Os contadores agora são tratados pelo shared_state centralizado.
// Removemos button_a_counter e button_b_counter daqui.

uint32_t button_handler_get_a_count(void) {
  return shared_state_get_pending_clicks();
}

uint32_t button_handler_get_b_count(void) {
  // No momento, o shared_state só tem um contador de cliques pendentes
  // genérico. Vou manter essa função retornando 0 ou o mesmo valor por
  // enquanto, conforme a estrutura atual do shared_state.
  return 0;
}

static void gpio_irq_callback(uint gpio, uint32_t events) {
  uint64_t now = time_us_64();

  if (gpio == BUTTON1_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_a_us >= DEBOUNCE_US) {
      last_a_us = now;
      shared_state_increment_pending_clicks();
      printf("[IRQ] Button A pressed! (Shared State Increment)\n");
    }
  } else if (gpio == BUTTON2_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_b_us >= DEBOUNCE_US) {
      last_b_us = now;
      // Botão B por enquanto não incrementa nada no shared_state ou
      // podemos adicionar outro campo se necessário.
      printf("[IRQ] Button B pressed!\n");
    }
  }
}

void button_handler_init(void) {
  // Botão A
  gpio_init(BUTTON1_PIN);
  gpio_set_dir(BUTTON1_PIN, GPIO_IN);
  gpio_pull_up(BUTTON1_PIN);

  // Botão B
  gpio_init(BUTTON2_PIN);
  gpio_set_dir(BUTTON2_PIN, GPIO_IN);
  gpio_pull_up(BUTTON2_PIN);

  // Registra callback único no Core 0 e habilita IRQ para ambos
  gpio_set_irq_enabled_with_callback(BUTTON1_PIN, GPIO_IRQ_EDGE_FALL, true,
                                     &gpio_irq_callback);
  gpio_set_irq_enabled(BUTTON2_PIN, GPIO_IRQ_EDGE_FALL, true);
}
