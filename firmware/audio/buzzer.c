#include "buzzer.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware_config.h"
#include "pico/stdlib.h"
#include "pico/time.h"

static uint slice_num;

// Callback para desligar o buzzer após a duração especificada
static int64_t buzzer_off_callback(alarm_id_t id, void *user_data) {
  pwm_set_chan_level(slice_num, PWM_CHAN_B, 0); // Define duty cycle como 0
  return 0;                                     // Não repete o alarme
}

void buzzer_init(void) {
  // Configura o pino como PWM
  gpio_set_function(BUZZER_A_PIN, GPIO_FUNC_PWM);

  // Obtém o número do slice PWM para o pino
  slice_num = pwm_gpio_to_slice_num(BUZZER_A_PIN);

  // Inicializa o PWM com nível 0 (desligado)
  pwm_set_clkdiv(slice_num, 1.0f);
  pwm_set_wrap(slice_num, 65535);
  pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);
  pwm_set_enabled(slice_num, true);
}

void buzzer_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (freq_hz == 0)
    return;

  // Calcula o wrap para a frequência desejada
  // freq = clock / (wrap + 1)
  // wrap = (clock / freq) - 1
  uint32_t clock_hz = clock_get_hz(clk_sys);
  uint32_t wrap = (clock_hz / freq_hz) - 1;

  // Se o wrap for maior que o suportado pelo hardware (16 bits), ajusta o
  // divisor
  float div = 1.0f;
  if (wrap > 65535) {
    div = (float)wrap / 65535.0f;
    wrap = 65535;
  }

  pwm_set_clkdiv(slice_num, div);
  pwm_set_wrap(slice_num, wrap);

  // Duty cycle baixo para não ser irritante (aprox 5% do wrap)
  pwm_set_chan_level(slice_num, PWM_CHAN_B, wrap / 20);

  // Agenda o desligamento
  add_alarm_in_ms(duration_ms, buzzer_off_callback, NULL, false);
}
