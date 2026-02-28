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
    // Envia 24 bits (G, R, B) empacotados no topo do registrador de 32 bits
    // MSB first (shift_right=false no PIO), portanto os dados ocupam os bits 31
    // downto 8.
    uint32_t color = ((uint32_t)led_buffer[i].G << 24) |
                     ((uint32_t)led_buffer[i].R << 16) |
                     ((uint32_t)led_buffer[i].B << 8);
    pio_sm_put_blocking(pio_inst, sm_inst, color);
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

// Função para converter coordenadas (x, y) para o índice do LED no BitDogLab
// x: 0 (esquerda) a 4 (direita)
// y: 0 (baixo) a 4 (cima)
static int get_index(int x, int y) {
  // No BitDogLab, a matriz começa no canto inferior direito
  // e segue em serpentina.
  if (y % 2 == 0) {
    // Linhas pares: da direita para a esquerda (y=0, 2, 4)
    return y * 5 + (4 - x);
  } else {
    // Linhas ímpares: da esquerda para a direita (y=1, 3)
    return y * 5 + x;
  }
}

// Bitmaps corrigidos para matriz 5x5 (25 bits)
// Ordem dos bits: Bit 24 (Topo-Esquerda, x=0, y=4) até Bit 0 (Base-Direita,
// x=4, y=0)
static const uint32_t final_bitmaps[10] = {
    0x0E8C62E, // 0
    0x046108E, // 1
    0x1F0FE1F, // 2
    0x1F0FC3F, // 3
    0x118FC21, // 4
    0x1F87C3F, // 5
    0x1F87E3F, // 6
    0x1F08888, // 7
    0x1F8FE3F, // 8
    0x1F8FC3F  // 9
};

void led_matrix_draw_number(uint8_t num, uint8_t r, uint8_t g, uint8_t b) {
  if (num > 9)
    return;

  uint32_t bitmap = final_bitmaps[num];

  // Limpa o buffer antes de desenhar
  for (int i = 0; i < 25; i++) {
    led_buffer[i].R = 0;
    led_buffer[i].G = 0;
    led_buffer[i].B = 0;
  }

  // Mapeia o bitmap de 25 bits (y=4..0, x=0..4) para os LEDs serpentina
  for (int y = 0; y < 5; y++) {
    for (int x = 0; x < 5; x++) {
      // Bit pos no bitmap: bit 24 é x=0, y=4. bit 0 é x=4, y=0.
      int bit_pos = y * 5 + (4 - x);
      if (bitmap & (1 << bit_pos)) {
        int index = get_index(x, y);
        led_buffer[index].R = r;
        led_buffer[index].G = g;
        led_buffer[index].B = b;
      }
    }
  }
  np_write();
}

void led_matrix_set_all(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < 25; i++) {
    led_buffer[i].R = r;
    led_buffer[i].G = g;
    led_buffer[i].B = b;
  }
  np_write();
}
