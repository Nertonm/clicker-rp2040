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
#include "debug_log.h"
#include "discovery/service_disc.h"
#include "hardware_config.h"
#include "middleware/app_queues.h"
#include "middleware/lamport.h"
#include "middleware/shared_state.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"
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

  /* Tenta conectar ao WiFi com retry periódico */
  TickType_t last_wifi_attempt = 0;
wifi_connect:
  if (last_wifi_attempt == 0 || (xTaskGetTickCount() - last_wifi_attempt) >=
                                    pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS)) {
    last_wifi_attempt = xTaskGetTickCount();

    if (setup_wifi()) {
      goto wifi_connected;
    }
    shared_state_set_connection_status(STATUS_OFFLINE);
    printf("[WIFI] Próxima tentativa em %d segundos\n",
           WIFI_RETRY_INTERVAL_MS / 1000);
  }

  /* Loop de modo offline com retry periódico */
  {
    uint32_t offline_loop_iter = 0;
    while (1) {
      offline_loop_iter++;

      if (shared_state_take_turbo_activation_requested()) {
        apply_local_turbo(TURBO_DURATION_MS);
        LOG_NORMAL("[TURBO]", "Ativado em modo offline duracao_ms=%u",
                   TURBO_DURATION_MS);
      }

      /* Cliques ficam em pending_clicks (shared_state); task_buttons não usa
       * mais queue_clicks, então não há nada para drenar aqui. O display
       * mostra pending_clicks via core1_display/task_display automaticamente.
       */

      /* Verifica se é hora de tentar reconectar WiFi */
      if ((xTaskGetTickCount() - last_wifi_attempt) >=
          pdMS_TO_TICKS(WIFI_RETRY_INTERVAL_MS)) {
        printf("[WIFI] Tentando reconexão...\n");
        goto wifi_connect;
      }

      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

wifi_connected:; /* empty statement: label cannot precede declaration in C99 */

  RpcSimpleResult init_result = rpc_init();

  setup_network_target();
  if (!init_result.success) {
    shared_state_set_connection_status(STATUS_OFFLINE);
  }

  bool registered = false;
  TickType_t last_scores_refresh = xTaskGetTickCount();
  TickType_t last_register_attempt = 0;
  uint16_t consecutive_rpc_failures = 0;
  uint8_t consecutive_register_failures = 0;
  uint64_t last_heartbeat_us =
      0; /* Timestamp do último heartbeat bem-sucedido */

  /* Backoff exponencial para retry de registro (definido em firmware_config.h)
   */
  static const uint32_t register_backoff_ms[] = {
      RPC_REGISTER_BACKOFF_1, RPC_REGISTER_BACKOFF_2, RPC_REGISTER_BACKOFF_3,
      RPC_REGISTER_BACKOFF_MAX};

  static uint32_t loop_count = 0;
/* Intervalo para dump de stats peridiocs (a cada 500 iterações) */
#define STATS_DUMP_INTERVAL 500u
  while (1) {
    /* Marca o início do ciclo de 20ms para compensação de tempo ao final */
    uint64_t cycle_start = time_us_64();

    loop_count++;
    if ((loop_count % 100) == 0) {
      LOG_VERBOSE("[RPC]", "loop #%lu", (unsigned long)loop_count);
    }
    /* Dump periódico de estatísticas a cada STATS_DUMP_INTERVAL iterações */
    if ((loop_count % STATS_DUMP_INTERVAL) == 0) {
      rpc_print_diagnostics();
      lamport_print_diagnostics();
    }

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
      /* Calcula delay com backoff exponencial */
      uint8_t backoff_idx = consecutive_register_failures;
      if (backoff_idx > 3)
        backoff_idx = 3;
      uint32_t current_retry_ms = register_backoff_ms[backoff_idx];

      if ((last_register_attempt == 0) ||
          ((now_ticks - last_register_attempt) >=
           pdMS_TO_TICKS(current_retry_ms))) {
        last_register_attempt = now_ticks;
        shared_state_set_connection_status(STATUS_CONNECTING);
        RpcSimpleResult reg = rpc_register_node((uint8_t)NODE_ID);
        if (reg.success) {
          registered = true;
          consecutive_rpc_failures = 0;
          consecutive_register_failures = 0;
          last_heartbeat_us = time_us_64(); /* Inicia contador do heartbeat a
                                               partir do registro */

          /* --- Início da lógica de sincronização --- */

          /* Pega todos os cliques pendentes para sincronizar */
          uint32_t total_to_sync = shared_state_take_pending_clicks();

          if (total_to_sync > 0) {
            shared_state_set_syncing_count(total_to_sync);
            shared_state_set_connection_status(STATUS_SYNCING);
            printf("[RPC] Sincronizando %lu cliques pendentes...\n",
                   (unsigned long)total_to_sync);

            uint32_t lamport_sent = lamport_tick();
            RpcClickResult sync_res =
                rpc_sync_offline((int)total_to_sync, lamport_sent);

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
            } else {
              printf(
                  "[RPC] ERRO: Sync falhou (erro=%d), voltando para OFFLINE\n",
                  sync_res.error_code);
              shared_state_restore_clicks(total_to_sync);
              shared_state_set_syncing_count(0);
              shared_state_set_connection_status(STATUS_OFFLINE);
              registered = false;
              continue;
            }
          }
          /* --- Fim da lógica de sincronização --- */

          shared_state_set_syncing_count(0);
          shared_state_set_connection_status(STATUS_ONLINE);
          shared_state_set_server_error_active(false);
          printf("[RPC] Nó %d registrado e sincronizado\n", NODE_ID);

          /* Atualiza scores imediatamente para o display mostrar valores
           * corretos mesmo se o nó ficar idle (sem clicar). Sem isso,
           * local_score e global_score ficam em 0 até o primeiro rpc_add_clicks
           * bem-sucedido. */
          {
            RpcScoreResult boot_scores = rpc_get_scores();
            if (boot_scores.success) {
              uint32_t boot_node_scores[MAX_NODES] = {0};
              for (uint8_t i = 0; i < MAX_NODES; i++) {
                boot_node_scores[i] = (boot_scores.node_scores[i] >= 0)
                                          ? (uint32_t)boot_scores.node_scores[i]
                                          : 0;
              }
              uint32_t boot_global = (boot_scores.global_score >= 0)
                                         ? (uint32_t)boot_scores.global_score
                                         : 0;
              shared_state_set_global_score(boot_global);
              shared_state_set_node_scores(boot_node_scores, MAX_NODES);
              shared_state_set_local_score(
                  boot_node_scores[NODE_ID % MAX_NODES]);
            }
          }
        } else {
          consecutive_register_failures++;
          shared_state_set_connection_status(STATUS_OFFLINE);
          printf("[RPC] Registro falhou, próximo retry em %lu ms\n",
                 (unsigned long)
                     register_backoff_ms[consecutive_register_failures > 3
                                             ? 3
                                             : consecutive_register_failures]);
        }
      }

      /* Quando offline, task_buttons mantém cliques em pending_clicks,
       * então não precisa drenar a fila aqui */
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    /* Processamento de Cliques Pendentes — ciclo de 20ms (US-38)
     * Consome diretamente do shared_state; task_buttons não usa a fila
     * no modo online, eliminando a corrida entre os dois consumidores.
     * IMPORTANTE: só consome quando status=ONLINE. Caso contrário os cliques
     * ficam em pending_clicks e serão sincronizados via sync_offline no
     * próximo registro. */
    uint32_t pending_clicks = 0;
    connection_status_t current_status = shared_state_get_connection_status();
    if (current_status == STATUS_ONLINE) {
      pending_clicks = shared_state_take_pending_clicks();
    }

    if (pending_clicks > 0) {
      /* Envia cliques para o servidor */
      uint32_t lamport_sent = lamport_tick();
      RpcClickResult click_res =
          rpc_add_clicks((int)pending_clicks, lamport_sent);

      if (click_res.success) {
        LOG_VERBOSE("[LAMPORT]", "sent=%lu recv=%lu monotonic=%s",
                    (unsigned long)lamport_sent,
                    (unsigned long)click_res.lamport_ts,
                    click_res.lamport_ts > lamport_sent ? "OK" : "VIOLATION");

        /* Reset do contador de falhas em caso de sucesso */
        consecutive_rpc_failures = 0;

        lamport_update((uint32_t)click_res.lamport_ts);
        shared_state_set_scores(&click_res);

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
          /* Rate limit: servidor aceitou apenas parte dos cliques.
           * Atualiza scores com os cliques aceitos e restaura os rejeitados. */
          if (click_res.accepted_clicks > 0) {
            lamport_update((uint32_t)click_res.lamport_ts);
            shared_state_set_scores(&click_res);
          }
          uint32_t rejected =
              pending_clicks - (uint32_t)click_res.accepted_clicks;
          if (rejected > 0) {
            shared_state_restore_clicks(rejected);
          }
          consecutive_rpc_failures = 0; /* Rate limit não é falha de rede */
        } else {
          shared_state_restore_clicks(pending_clicks);
          if (click_res.error_code == RPC_LAMPORT_VIOLATION) {
            lamport_update((uint32_t)click_res.lamport_ts);
            /* Violação de Lamport não conta como falha de rede */
          } else {
            /* Falha de rede (timeout, desconexão, parse error, etc). */
            consecutive_rpc_failures++;
            LOG_NORMAL("[RPC]", "Falha RPC #%d/3", consecutive_rpc_failures);
          }
        }
      }
    }

    /* Heartbeat periódico: reenvia rpc_register_node a cada 30 s para manter
     * o nó ACTIVE no NodeRegistry. Verificado a cada ciclo de 20 ms via
     * time_us_64() — sem timer de hardware separado. */
    if (shared_state_get_connection_status() == STATUS_ONLINE) {
      uint64_t hb_now = time_us_64();
      if ((hb_now - last_heartbeat_us) >= HEARTBEAT_INTERVAL_US) {
        RpcSimpleResult hb_result = rpc_register_node((uint8_t)NODE_ID);
        if (hb_result.success) {
          last_heartbeat_us = time_us_64();
          LOG_NORMAL("[HEARTBEAT]", "HEARTBEAT SENT node_id=%d", NODE_ID);
        } else {
          consecutive_rpc_failures++;
          LOG_NORMAL("[HEARTBEAT]", "HEARTBEAT FAILED consecutivas=%d",
                     consecutive_rpc_failures);
        }
      }
    }

    /* Avalia transição para OFFLINE independentemente de pending_clicks.
     * Garante que falhas de heartbeat (sem cliques) também transitam o nó
     * para OFFLINE após 3 falhas consecutivas. */
    if (consecutive_rpc_failures >= 3) {
      LOG_NORMAL("[RPC]",
                 "3 falhas consecutivas — transicao OFFLINE "
                 "pending_clicks=%lu",
                 (unsigned long)shared_state_get_pending_clicks());
      shared_state_set_connection_status(STATUS_OFFLINE);
      consecutive_rpc_failures = 0;
      registered = false;
    }

    /* Atualização Periódica do Placar Global.
     * Executado ANTES do cálculo de elapsed para que sua latência seja
     * contabilizada no orçamento de 20ms da compensação de tempo. */
    TickType_t now = xTaskGetTickCount();
    if ((now - last_scores_refresh) >= pdMS_TO_TICKS(SCORES_REFRESH_MS)) {
      last_scores_refresh = now;
      RpcScoreResult scores = rpc_get_scores();
      if (scores.success) {
        uint32_t node_scores[MAX_NODES] = {0};
        for (uint8_t i = 0; i < MAX_NODES; i++) {
          node_scores[i] = (scores.node_scores[i] >= 0)
                               ? (uint32_t)scores.node_scores[i]
                               : 0;
        }
        uint32_t global =
            (scores.global_score >= 0) ? (uint32_t)scores.global_score : 0;
        shared_state_set_global_score(global);
        shared_state_set_node_scores(node_scores, MAX_NODES);

        /* Só sobrescreve local_score se o servidor retornou valor > 0.
         * Evita zerar o display quando o nó ainda não aparece no backend. */
        uint32_t received_local = node_scores[NODE_ID % MAX_NODES];
        if (received_local > 0) {
          shared_state_set_local_score(received_local);
        }

        shared_state_set_connection_status(STATUS_ONLINE);
        shared_state_set_server_error_active(false);
      } else {
        printf("[RPC] Falha ao atualizar scores (ignorando)\n");
      }
    }

    /* Compensação de tempo: dorme apenas o restante do período de 20ms. */
    uint64_t elapsed = time_us_64() - cycle_start;
    LOG_VERBOSE("[RPC]", "ciclo_us=%llu pending=%lu",
                (unsigned long long)elapsed, (unsigned long)pending_clicks);
    if (elapsed < CYCLE_PERIOD_US) {
      uint32_t sleep_us = (uint32_t)(CYCLE_PERIOD_US - elapsed);
      TickType_t sleep_ticks = pdMS_TO_TICKS(sleep_us / 1000u);
      if (sleep_ticks > 0) {
        vTaskDelay(sleep_ticks);
      } else {
        taskYIELD();
      }
    } else {
      taskYIELD();
    }
  }
}
