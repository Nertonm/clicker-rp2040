#include "audio/buzzer.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware/adc.h"
// #include "hardware/pwm.h" // Remoção de header não utilizado
#include "middleware/button_handler.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "secrets_template.h"
#include "ssd1306_i2c.h"
#include <stdio.h>

#include "hardware_config.h"

int main(void) {
  stdio_init_all();

  // Aguarda conexão do terminal USB para não perder mensagens iniciais
  while (!stdio_usb_connected()) {
    sleep_ms(100);
  }

  printf("\n[BOOT] Inicializando hardware...\n");

  display_init();
  display_clear();
  display_text(0, 0, "Hello World");
  display_show();

  button_handler_init();

  // Teste do Buzzer (US-03 adjacente)
  buzzer_init();

  // Teste da Matriz de LEDs (US-03)
  led_matrix_init();
  led_set(0, 150, 0, 0);  // Vermelho no primeiro LED (canto inferior direito)
  led_set(24, 0, 0, 150); // Azul no último LED (canto superior esquerdo)

  // Inicializa ADC para evitar travamento no adc_read()
  adc_init();
  adc_gpio_init(JOY_VRY_PIN); // GPIO 26 = ADC 0
  adc_select_input(0);

  if (cyw43_arch_init()) {
    printf("failed to initialise WiFi chip, seguindo sem WiFi\n");
    // marcar STATUS_OFFLINE e seguir normalmente
  } else {
    cyw43_arch_enable_sta_mode();
    printf("Connecting to Wi-Fi...\n");

    int rc = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                                CYW43_AUTH_WPA2_AES_PSK, 10000);

    if (rc != 0) {
      printf("failed to connect, rc=%d — seguindo sem WiFi\n", rc);
      // não chama exit, não entra em while(1) de erro
      // apenas considera STATUS_OFFLINE e segue
    } else {
      printf("WiFi connected.\n");
      // STATUS_ONLINE
    }
  }

  uint32_t last_a = 0;
  uint32_t last_b = 0;

  printf("\n[MAIN] Entrando no loop principal...\n");

  while (true) {
    uint32_t count_a = button_handler_get_a_count();
    uint32_t count_b = button_handler_get_b_count();

    if (count_a != last_a || count_b != last_b) {
      printf("[BTN] A: %u | B: %u\n", count_a, count_b);

      // Atualiza o display OLED com o contador do botão A
      char buf[32];
      sprintf(buf, "Button A: %u", count_a);
      display_clear();
      display_text(0, 0, "Counter Status");
      display_text(2, 0, buf);
      display_show();

      // Atualiza a matriz de LEDs (US-03 aprimorada)
      uint8_t digito = count_a % 10;

      // Se for múltiplo de 10 (milestone), pisca dourado e mantém o 0 dourado
      if (digito == 0 && count_a > 0) {
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

      last_a = count_a;
      last_b = count_b;

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