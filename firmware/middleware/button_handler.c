#include "button_handler.h"
#include <stdio.h>

#include "hardware_config.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

// Variáveis para debouncing
static volatile uint64_t last_a_us = 0;
static volatile uint64_t last_b_us = 0;

// Handlers para os botões
static volatile uint32_t button_a_counter = 0;
static volatile uint32_t button_b_counter = 0;

// Expor os contadores para o main() via getter
uint32_t button_handler_get_a_count(void) { return button_a_counter; }
uint32_t button_handler_get_b_count(void) { return button_b_counter; }

static void gpio_irq_callback(uint gpio, uint32_t events) {
  uint64_t now = time_us_64();

  if (gpio == BUTTON1_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_a_us >= DEBOUNCE_US) {
      last_a_us = now;
      button_a_counter++;
      printf("[IRQ] Button A pressed! Count: %u\n", button_a_counter);
    }
  } else if (gpio == BUTTON2_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
    if (now - last_b_us >= DEBOUNCE_US) {
      last_b_us = now;
      button_b_counter++;
      printf("[IRQ] Button B pressed! Count: %u\n", button_b_counter);
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
