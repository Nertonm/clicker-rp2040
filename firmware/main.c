#include "audio/buzzer.h"
#include "drivers/display/display.h"
#include "hardware/adc.h"
// #include "hardware/pwm.h" // Remoção de header não utilizado
#include "middleware/button_handler.h"
#include "middleware/core1_display.h"
#include "middleware/shared_state.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "hardware_config.h"

/**
 * @brief Simula o envio de cliques via RPC.
 * Retorna true para sucesso, false para falha simulada.
 */
bool send_clicks_rpc(uint32_t clicks) {
  static uint32_t attempt = 0;
  attempt++;

  // Simula uma falha a cada 3 tentativas para testar o restore
  if (attempt % 3 == 0) {
    printf("[RPC] Simulated FAILURE for %u clicks\n", clicks);
    return false;
  }

  printf("[RPC] Success: sent %u clicks\n", clicks);
  return true;
}

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

  // Inicializa ADC
  adc_init();
  adc_gpio_init(JOY_VRY_PIN);
  adc_select_input(0);

  // Lança o Core 1 ANTES da conexão WiFi para podermos ver "CONNECTING..." no
  // OLED
  multicore_launch_core1(core1_display_entry);

  if (cyw43_arch_init()) {
    printf("failed to initialise WiFi chip, seguindo sem WiFi\n");
    shared_state_set_connection_status(STATUS_OFFLINE);
  } else {
    cyw43_arch_enable_sta_mode();

    // Loop de tentativa de conexão com retries
    shared_state_set_connection_status(STATUS_CONNECTING);
    while (true) {
      printf("[WIFI] Connecting to SSID '%s'...\n", WIFI_SSID);
      int rc = cyw43_arch_wifi_connect_timeout_ms(
          WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000);

      if (rc == 0) {
        printf("[WIFI] Connected.\n");
        shared_state_set_connection_status(STATUS_ONLINE);
        break; // Segue para iniciar o loop principal
      } else {
        printf("[WIFI] Connect failed, rc=%d. Retrying in 5s...\n", rc);
        shared_state_set_connection_status(STATUS_OFFLINE);
        sleep_ms(5000);
        shared_state_set_connection_status(STATUS_CONNECTING);
      }
    }
  }

  // Placar local acumulado apenas após confirmação do "servidor"
  uint32_t local_score_confirmed = 0;

  printf("\n[MAIN] Entrando no loop principal...\n");

  static uint32_t last_logged_pending = 0;
  static uint32_t ms_since_last_rpc_attempt = 0;
  const uint32_t RPC_COOLDOWN_MS = 1000; // 1s de cooldown após falha

  while (true) {
    cyw43_arch_poll(); // Necessário para processar eventos de rede em modo
                       // NO_SYS

    uint32_t current_pending = shared_state_get_pending_clicks();

    if (current_pending > last_logged_pending && current_pending > 0) {
      printf("[BTN_MONITOR] pending_clicks cresceu para: %u\n",
             current_pending);
      last_logged_pending = current_pending;
    } else if (current_pending == 0) {
      last_logged_pending = 0;
    }

    if (ms_since_last_rpc_attempt >= RPC_COOLDOWN_MS) {
      // Core 0 consome cliques pendentes de forma atômica (US-04)
      uint32_t batch_clicks = shared_state_take_pending_clicks();

      if (batch_clicks > 0) {
        printf("[RPC_TX] Consumindo batch atômico de %u cliques. Iniciando "
               "envio...\n",
               batch_clicks);

        if (send_clicks_rpc(batch_clicks)) {
          // Sucesso: atualiza placar e feedback
          local_score_confirmed += batch_clicks;
          shared_state_set_local_score(local_score_confirmed);

          printf("[RPC_OK] Placar confirmado atualizado para: %u\n",
                 local_score_confirmed);

          // Sinaliza eventos para o Core 1
          if (local_score_confirmed % 10 == 0 && local_score_confirmed > 0) {
            shared_state_set_milestone_triggered(true);
          } else {
            shared_state_set_led_flash_requested(true);
          }
          buzzer_tone(2000, 20);
        } else {
          // Falha: restaura cliques para o próximo batch (merge)
          shared_state_restore_clicks(batch_clicks);
          printf("[RPC_FAIL] Restore crítico: devolvendo %u cliques ao pool. "
                 "(Merge)\n",
                 batch_clicks);

          ms_since_last_rpc_attempt = 0; // Inicia cooldown

          // Opcional: feedback de erro (tom mais grave)
          buzzer_tone(500, 50);
        }
      }
    } else {
      ms_since_last_rpc_attempt += 10;
    }

    // Loop principal leve não bloqueia, não trava
    sleep_ms(10);
  }
}