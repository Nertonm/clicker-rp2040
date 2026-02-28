#include "audio/buzzer.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware/adc.h"
// #include "hardware/pwm.h" // Remoção de header não utilizado
#include "middleware/button_handler.h"
#include "middleware/core1_display.h"
#include "middleware/shared_state.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "secrets_template.h"
#include <stdio.h>

#include "hardware_config.h"

int main(void) {
  stdio_init_all();

  // Aguarda conexão do terminal USB para não perder mensagens iniciais
  while (!stdio_usb_connected()) {
    sleep_ms(100);
  }

  printf("\n[BOOT] Inicializando hardware...\n");

  // Inicializa o Estado Compartilhado ANTES de qualquer outro middleware
  shared_state_init();

  display_init();

  button_handler_init();
  buzzer_init();
  led_matrix_init();

  // Inicializa ADC
  adc_init();
  adc_gpio_init(JOY_VRY_PIN);
  adc_select_input(0);

  if (cyw43_arch_init()) {
    printf("failed to initialise WiFi chip, seguindo sem WiFi\n");
    shared_state_set_connection_status(STATUS_OFFLINE);
  } else {
    cyw43_arch_enable_sta_mode();
    shared_state_set_connection_status(STATUS_CONNECTING);
    printf("Connecting to Wi-Fi...\n");

    int rc = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                                CYW43_AUTH_WPA2_AES_PSK, 10000);

    if (rc != 0) {
      printf("failed to connect, rc=%d — seguindo sem WiFi\n", rc);
      shared_state_set_connection_status(STATUS_OFFLINE);
    } else {
      printf("WiFi connected.\n");
      shared_state_set_connection_status(STATUS_ONLINE);
    }
  }

  // Variável local para acumular o placar processado
  uint32_t local_score_total = 0;

  // Lança o Core 1 para cuidar do display
  multicore_launch_core1(core1_display_entry);

  printf("\n[MAIN] Entrando no loop principal...\n");

  while (true) {
    // Core 0 consome cliques pendentes de forma atômica (US-04)
    uint32_t pending = shared_state_take_pending_clicks();

    if (pending > 0) {
      local_score_total += pending;
      // No futuro, isso será enviado via RPC e o servidor retornará o
      // global_score
      shared_state_set_local_score(local_score_total);

      printf("[BTN] Consumed: %u | Total: %u\n", pending, local_score_total);

      // Atualiza a matriz de LEDs (US-03 aprimorada)
      uint8_t digito = local_score_total % 10;

      // Se for múltiplo de 10 (milestone), pisca dourado e mantém o 0 dourado
      if (digito == 0 && local_score_total > 0) {
        // Blink duplo dourado em toda a matriz (suave via driver)
        for (int j = 0; j < 2; j++) {
          led_matrix_set_all(100, 80, 0);
          sleep_ms(150);
          led_clear_all();
          sleep_ms(100);
        }
        // Mostra o número 0 em dourado
        led_matrix_draw_number(0, 100, 80, 0);
      } else {
        // Agora com o driver limitando, valor 30 fica muito suave
        led_matrix_draw_number(digito, 30, 30, 30);
      }

      // Feedback visual muito fraco ao apertar botão (LED central)
      led_set(12, 0, 0, 10);
      buzzer_tone(2000, 20);
      sleep_ms(50);
      led_set(12, 0, 0, 0);
    }

    // Pisca o LED central a cada ~1s para indicar "estamos vivos"
    static uint32_t last_blink = 0;
    if (to_ms_since_boot(get_absolute_time()) - last_blink > 1000) {
      last_blink = to_ms_since_boot(get_absolute_time());
      static bool state = false;
      state = !state;
      if (state)
        led_set(12, 20, 20, 20); // Branco fraquinho
      else
        led_set(12, 0, 0, 0);
    }

    // Loop principal leve — não bloqueia, não trava
    sleep_ms(10);
  }
}