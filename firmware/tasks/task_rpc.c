/**
 * @file task_rpc.c
 * @brief Implementação da tarefa de comunicação RPC e gerenciamento de rede.
 *
 * Esta tarefa orquestra a conexão WiFi, a descoberta do servidor e a
 * sincronização contínua de cliques e pontuações com o backend central.
 *
 * @author
 * @date 2026-03-01
 */

#include "FreeRTOS.h"
#include "task.h"

#include "config/firmware_config.h"
#include "config/net_config.h"
#include "discovery/service_disc.h"
#include "hardware_config.h"
#include "middleware/app_queues.h"
#include "middleware/lamport.h"
#include "middleware/shared_state.h"
#include "pico/cyw43_arch.h"
#include "rpc_client.h"
#include "task_rpc.h"
#include <stdio.h>

/**
 * @brief Aplica o efeito visual de Turbo localmente.
 *
 * @param[in] duration_ms Duração do efeito em milissegundos.
 */
static void apply_local_turbo(uint32_t duration_ms) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  shared_state_set_turbo_until_ms(now + duration_ms);
  shared_state_set_turbo_active(true);
}

/**
 * @brief Realiza a busca do servidor na rede e configura o cliente RPC.
 *
 * Tenta descobrir o servidor via UDP Broadcast. Caso falhe, utiliza
 * o endereço IP de fallback definido nas configurações.
 */
static void setup_network_target(void) {
  ip_addr_t discovered_ip;
  uint16_t discovered_port = 0;

  absolute_time_t timeout = make_timeout_time_ms(3000);
  if (service_disc_discover(&discovered_ip, &discovered_port, timeout)) {
    const char *ip_str = ip4addr_ntoa(ip_2_ip4(&discovered_ip));
    rpc_client_set_server(ip_str, discovered_port);
    shared_state_set_fallback_in_use(false);
    printf("[DISC] Servidor descoberto em %s:%u\n", ip_str,
           (unsigned)discovered_port);
  } else {
    rpc_client_set_server_fallback();
    shared_state_set_fallback_in_use(true);
    printf("[DISC] Discovery falhou, usando fallback\n");
  }
}

/**
 * @brief Inicializa e conecta ao ponto de acesso WiFi.
 *
 * @return bool Verdadeiro se a conexão foi estabelecida com sucesso.
 */
static bool setup_wifi(void) {
  cyw43_arch_enable_sta_mode();

  if (WIFI_SSID[0] == '\0' || WIFI_PASSWORD[0] == '\0') {
    printf("[WIFI] Credenciais ausentes; modo offline\n");
    shared_state_set_connection_status(STATUS_OFFLINE);
    return false;
  }

  printf("[WIFI] Conectando em %s...\n", WIFI_SSID);
  if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                         CYW43_AUTH_WPA2_AES_PSK, 15000)) {
    printf("[WIFI] Falha de conexão\n");
    shared_state_set_connection_status(STATUS_OFFLINE);
    return false;
  }

  printf("[WIFI] Conectado\n");
  shared_state_set_connection_status(STATUS_CONNECTING);
  return true;
}

/**
 * @brief Loop principal da tarefa de rede RPC.
 */
void task_rpc(void *param) {
  (void)param;

  QueueHandle_t queue_clicks = app_queues_get_clicks();

  if (cyw43_arch_init()) {
    printf("[RPC] ERRO: falha ao inicializar CYW43 (task)\n");
    shared_state_set_connection_status(STATUS_OFFLINE);
    while (1) {
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }

  /* Tenta conectar ao WiFi */
  bool wifi_ok = setup_wifi();
  if (!wifi_ok) {
    shared_state_set_connection_status(STATUS_OFFLINE);
    /* Loop infinito em modo offline se WiFi falhar */
    while (1) {
      if (shared_state_take_turbo_activation_requested()) {
        apply_local_turbo(TURBO_DURATION_MS);
        printf("[TURBO] Ativado em modo offline por %u ms\n",
               TURBO_DURATION_MS);
      }
      click_msg_t msg;
      (void)xQueueReceive(queue_clicks, &msg, pdMS_TO_TICKS(500));
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  setup_network_target();

  RpcSimpleResult init_result = rpc_init();
  if (!init_result.success) {
    shared_state_set_connection_status(STATUS_OFFLINE);
  }

  bool registered = false;
  TickType_t last_scores_refresh = xTaskGetTickCount();
  TickType_t last_register_attempt = 0;
  uint8_t consecutive_rpc_failures = 0;

  while (1) {
    /* Processamento de Power-up (Turbo) */
    if (shared_state_take_turbo_activation_requested()) {
      uint32_t turbo_ms = TURBO_DURATION_MS;
      if (registered) {
        RpcPowerupResult powerup = rpc_activate_powerup();
        if (powerup.success && powerup.powerup_remaining_s > 0) {
          turbo_ms = (uint32_t)powerup.powerup_remaining_s * 1000u;
        }
      }
      apply_local_turbo(turbo_ms);
      printf("[TURBO] Ativado por %lu ms\n", (unsigned long)turbo_ms);
    }

    /* Lógica de Registro Inicial e Reconexão */
    if (!registered) {
      TickType_t now_ticks = xTaskGetTickCount();
      if ((last_register_attempt == 0) ||
          ((now_ticks - last_register_attempt) >=
           pdMS_TO_TICKS(RPC_REGISTER_RETRY_MS))) {
        last_register_attempt = now_ticks;
        shared_state_set_connection_status(STATUS_CONNECTING);
        RpcSimpleResult reg = rpc_register_node((uint8_t)NODE_ID);
        if (reg.success) {
          registered = true;
          consecutive_rpc_failures = 0; // Reset ao reconectar

          // --- Início da lógica de sincronização ---
          uint32_t pending_snapshot = shared_state_take_pending_clicks();

          if (pending_snapshot > 0) {
            shared_state_set_connection_status(STATUS_SYNCING);
            printf("[RPC] Sincronizando %lu cliques pendentes...\n",
                   (unsigned long)pending_snapshot);

            uint32_t lamport_sent = lamport_tick();
            RpcClickResult sync_res =
                rpc_sync_offline((int)pending_snapshot, lamport_sent);

            if (sync_res.success) {
              lamport_update((uint32_t)sync_res.lamport_ts);
              shared_state_set_scores(&sync_res);

              printf("[RPC] Sync completo: global=%d local=%d lamport=%lu\n",
                     sync_res.global_score, sync_res.local_score,
                     (unsigned long)sync_res.lamport_ts);

              if (sync_res.milestone_triggered) {
                shared_state_set_milestone_triggered(true);
                shared_state_set_led_flash_requested(true);
                printf("[MILESTONE] Marco atingido durante sync: %d\n",
                       sync_res.milestone_value);
              }

              vTaskDelay(pdMS_TO_TICKS(500));
            } else {
              printf("[RPC] ERRO: Sync falhou (erro=%d), voltando para OFFLINE\n",
                     sync_res.error_code);
              shared_state_restore_clicks(pending_snapshot);
              shared_state_set_connection_status(STATUS_OFFLINE);
              registered = false;
              continue;
            }
          }
          // --- Fim da lógica de sincronização ---

          shared_state_set_connection_status(STATUS_ONLINE);
          shared_state_set_server_error_active(false);
          printf("[RPC] Nó %d registrado\n", NODE_ID);
        } else {
          shared_state_set_connection_status(STATUS_OFFLINE);
        }
      }

      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    /* Processamento da Fila de Cliques Pendentes */
    click_msg_t msg;
    if (xQueueReceive(queue_clicks, &msg, pdMS_TO_TICKS(RPC_POLL_PERIOD_MS)) ==
        pdPASS) {
      connection_status_t current_status = shared_state_get_connection_status();

      if (current_status == STATUS_OFFLINE) {
        // Se estamos OFFLINE, não tenta RPC, apenas restaura cliques para acúmulo local
        shared_state_restore_clicks(msg.clicks);
      } else {
        // Apenas processa RPC se estiver ONLINE ou CONNECTING
        do {
          uint32_t lamport_sent = lamport_tick();
          RpcClickResult click_res = rpc_add_clicks((int)msg.clicks, lamport_sent);

          if (click_res.success) {
            printf("[LAMPORT] sent=%lu recv=%lu monotonic=%s\n",
                   (unsigned long)lamport_sent,
                   (unsigned long)click_res.lamport_ts,
                   click_res.lamport_ts > lamport_sent ? "OK" : "VIOLATION");

            // Reset do contador de falhas em caso de sucesso
            consecutive_rpc_failures = 0;

            lamport_update((uint32_t)click_res.lamport_ts);
            shared_state_set_scores(&click_res);                // atômico

            if (click_res.milestone_triggered) {
              shared_state_set_led_flash_requested(true);
              printf("[MILESTONE] Marco atingido: %d\n", click_res.milestone_value);
            }

            if (click_res.powerup_remaining_s > 0) {
              apply_local_turbo((uint32_t)click_res.powerup_remaining_s * 1000u);
              printf("[TURBO] Power-up do servidor: %d s restantes\n",
                     click_res.powerup_remaining_s);
            }
          } else {
            /* Tratamento de Erros do Servidor */
            if (click_res.error_code == RPC_RATE_EXCEEDED) {
              uint32_t rejected =
                  msg.clicks - (uint32_t)click_res.accepted_clicks;
              if (rejected > 0) {
                shared_state_restore_clicks(rejected);
              }
            } else {
              shared_state_restore_clicks(msg.clicks);
              if (click_res.error_code == RPC_LAMPORT_VIOLATION) {
                lamport_update((uint32_t)click_res.lamport_ts);
                // Violação de Lamport não conta como falha de rede
              } else {
                // Falha de rede (timeout, desconexão, parse error, etc)
                consecutive_rpc_failures++;
                printf("[RPC] Falha #%d/3\n", consecutive_rpc_failures);

                if (consecutive_rpc_failures >= 3) {
                  printf("[RPC] 3 falhas consecutivas, entrando em modo OFFLINE\n");
                  shared_state_set_connection_status(STATUS_OFFLINE);                  consecutive_rpc_failures = 0; // Reset para próximo ciclo
                  registered = false;           // Força re-registro na reconexão
                  break;
                }
              }
            }
          }
        } while (xQueueReceive(queue_clicks, &msg, 0) == pdPASS);
      }
    }

    /* Atualização Periódica do Placar Global */
    TickType_t now = xTaskGetTickCount();
    if ((now - last_scores_refresh) >= pdMS_TO_TICKS(SCORES_REFRESH_MS)) {
      last_scores_refresh = now;
      RpcScoreResult scores = rpc_get_scores();
      if (scores.success) {
        uint32_t node_scores[MAX_NODES] = {0};
        for (uint8_t i = 0; i < MAX_NODES; i++) {
          node_scores[i] = (uint32_t)scores.node_scores[i];
        }
        shared_state_set_global_score((uint32_t)scores.global_score);
        shared_state_set_node_scores(node_scores, MAX_NODES);
        shared_state_set_local_score(node_scores[NODE_ID % MAX_NODES]);
        shared_state_set_connection_status(STATUS_ONLINE);
        shared_state_set_server_error_active(false);
      } else {
        printf("[RPC] Falha ao atualizar scores (ignorando)\n");
      }
    }
  }
}
