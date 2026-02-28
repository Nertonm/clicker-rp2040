#include "ws2812.h"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware_config.h"
#include "pico/stdlib.h"
#include "ws2812.pio.h"
#include <stdio.h>

static PIO pio_inst;
static uint sm_inst;

// Estrutura de pixel seguindo o padrão BitDogLab (GRB)
typedef struct {
  uint8_t G, R, B;
} pixel_t;

static pixel_t led_buffer[WS2812_NUM_LEDS];

void led_matrix_init(void) {
  uint offset;
  printf("[WS2812] Inicializando modo BitDogLab (GPIO %d)...\n", WS2812_PIN);
  fflush(stdout);

  // Inicialização do pino
  gpio_init(WS2812_PIN);
  gpio_set_dir(WS2812_PIN, GPIO_OUT);
  gpio_put(WS2812_PIN, 0);
  sleep_us(300); // Reset pulse inicial

  // Tenta carregar o programa no PIO0 primeiro
  pio_inst = pio0;
  if (!pio_can_add_program(pio_inst, &ws2812_program)) {
    pio_inst = pio1;
  }

  offset = pio_add_program(pio_inst, &ws2812_program);
  sm_inst = pio_claim_unused_sm(pio_inst, true);

  ws2812_program_init(pio_inst, sm_inst, offset, WS2812_PIN, 800000.f, false);

  printf("[WS2812] Pronto: PIO %d, SM %d\n", pio_get_index(pio_inst), sm_inst);
  fflush(stdout);

  led_clear_all();
}

static void np_write(void) {
  for (uint i = 0; i < WS2812_NUM_LEDS; i++) {
    pio_sm_put_blocking(pio_inst, sm_inst, led_buffer[i].G);
    pio_sm_put_blocking(pio_inst, sm_inst, led_buffer[i].R);
    pio_sm_put_blocking(pio_inst, sm_inst, led_buffer[i].B);
  }
  sleep_us(100); // Latch pulse
}

void led_set(int index, uint8_t r, uint8_t g, uint8_t b) {
  if (index < 0 || index >= WS2812_NUM_LEDS)
    return;

  led_buffer[index].R = r;
  led_buffer[index].G = g;
  led_buffer[index].B = b;

  np_write();
}

void led_clear_all(void) {
  for (uint i = 0; i < WS2812_NUM_LEDS; i++) {
    led_buffer[i].R = 0;
    led_buffer[i].G = 0;
    led_buffer[i].B = 0;
  }
  np_write();
}
