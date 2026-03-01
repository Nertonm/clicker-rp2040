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

static void apply_local_turbo(uint32_t duration_ms) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  shared_state_set_turbo_until_ms(now + duration_ms);
  shared_state_set_turbo_active(true);
}

static void apply_score_update(const RpcClickResult *result) {
  uint32_t local_before = shared_state_get_local_score();
  shared_state_set_local_score((uint32_t)result->local_score);
  shared_state_set_global_score((uint32_t)result->global_score);
  shared_state_set_connection_status(STATUS_ONLINE);
  shared_state_set_server_error_active(false);
}

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

  bool wifi_ok = setup_wifi();
  if (!wifi_ok) {
    shared_state_set_connection_status(STATUS_OFFLINE);
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

  while (1) {
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

    click_msg_t msg;
    if (xQueueReceive(queue_clicks, &msg, pdMS_TO_TICKS(RPC_POLL_PERIOD_MS)) ==
        pdPASS) {
      do {
        int lamport_ts = lamport_tick();
        RpcClickResult click_res = rpc_add_clicks((int)msg.clicks, lamport_ts);

        if (click_res.success) {
          lamport_update(click_res.lamport_ts);
          apply_score_update(&click_res);
        } else {
          if (click_res.error_code == RPC_RATE_EXCEEDED) {
            uint32_t rejected =
                msg.clicks - (uint32_t)click_res.accepted_clicks;
            if (rejected > 0) {
              shared_state_restore_clicks(rejected);
            }
          } else {
            shared_state_restore_clicks(msg.clicks);
            if (click_res.error_code == RPC_LAMPORT_VIOLATION) {
              lamport_update(click_res.lamport_ts);
            } else {
              shared_state_set_connection_status(STATUS_OFFLINE);
              registered = false;
              break;
            }
          }
        }
      } while (xQueueReceive(queue_clicks, &msg, 0) == pdPASS);
    }

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
