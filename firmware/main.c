#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "audio/buzzer.h"
#include "discovery/service_disc.h"
#include "drivers/display/display.h"
#include "drivers/display/ws2812.h"
#include "hardware_config.h"
#include "middleware/button_handler.h"
#include "middleware/lamport.h"
#include "middleware/shared_state.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "rpc_client.h"

#include <stdio.h>

#ifdef STRESS_TEST
#include "tests/stress_test.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#define CLICK_QUEUE_LEN 32
#define DISPLAY_PERIOD_MS 150
#define MONITOR_PERIOD_MS 5000
#define RPC_POLL_PERIOD_MS 100
#define SCORES_REFRESH_MS 5000
#define RPC_REGISTER_RETRY_MS 2000
#define TURBO_DURATION_MS 10000
#define MILESTONE_GLOW_TICKS 10

typedef struct {
  uint32_t clicks;
} click_msg_t;

static QueueHandle_t queue_clicks;

static void setup_network_target(void);
static bool setup_wifi(void);

static void trigger_milestone_feedback(void) {
  static uint32_t last_trigger_ms = 0;
  uint32_t now_ms = to_ms_since_boot(get_absolute_time());

  if ((int32_t)(now_ms - last_trigger_ms) < 250) {
    return;
  }

  last_trigger_ms = now_ms;
  shared_state_set_milestone_triggered(true);
  buzzer_tone(1800, 120);
}

static void apply_local_turbo(uint32_t duration_ms) {
  uint32_t now = to_ms_since_boot(get_absolute_time());
  shared_state_set_turbo_until_ms(now + duration_ms);
  shared_state_set_turbo_active(true);
}

static void wheel_color(uint8_t pos, uint8_t *r, uint8_t *g, uint8_t *b) {
  uint8_t p = 255 - pos;
  if (p < 85) {
    *r = 255 - p * 3;
    *g = 0;
    *b = p * 3;
  } else if (p < 170) {
    p -= 85;
    *r = 0;
    *g = p * 3;
    *b = 255 - p * 3;
  } else {
    p -= 170;
    *r = p * 3;
    *g = 255 - p * 3;
    *b = 0;
  }
}

static const char *status_to_text(connection_status_t status) {
  switch (status) {
  case STATUS_ONLINE:
    return "ONLINE";
  case STATUS_OFFLINE:
    return "OFFLINE";
  case STATUS_CONNECTING:
    return "CONNECTING";
  case STATUS_SYNCING:
    return "SYNCING";
  default:
    return "UNKNOWN";
  }
}

static void apply_score_update(const RpcClickResult *result) {
  uint32_t local_before = shared_state_get_local_score();
  shared_state_set_local_score((uint32_t)result->local_score);
  shared_state_set_global_score((uint32_t)result->global_score);
  if (result->milestone_triggered &&
      (uint32_t)result->local_score != local_before) {
    trigger_milestone_feedback();
  }
  shared_state_set_connection_status(STATUS_ONLINE);
  shared_state_set_server_error_active(false);
}

static void task_buttons(void *param) {
  (void)param;

  while (1) {
    uint32_t pending = shared_state_take_pending_clicks();
    if (pending > 0) {
      shared_state_set_led_flash_requested(true);

      click_msg_t msg = {.clicks = pending};
      if (xQueueSend(queue_clicks, &msg, 0) != pdPASS) {
        shared_state_restore_clicks(pending);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

static void task_rpc(void *param) {
  (void)param;

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
    // Espera até 100ms pelo primeiro clique para dar fôlego ao sistema
    if (xQueueReceive(queue_clicks, &msg, pdMS_TO_TICKS(RPC_POLL_PERIOD_MS)) ==
        pdPASS) {
      do {
        int lamport_ts = lamport_tick();
        RpcClickResult click_res = rpc_add_clicks((int)msg.clicks, lamport_ts);

        if (click_res.success) {
          lamport_update(click_res.lamport_ts);
          apply_score_update(&click_res);
        } else {
          shared_state_set_connection_status(STATUS_OFFLINE);
          if (click_res.error_code != RPC_RATE_EXCEEDED) {
            registered = false;
          }
          break;
        }
      } while (xQueueReceive(queue_clicks, &msg, 0) == pdPASS);
    }

    rpc_poll();

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

static void task_display(void *param) {
  (void)param;

  char row1[24];
  char row2[24];
  char row3[24];
  uint8_t rainbow_phase = 0;
  uint8_t milestone_glow_ticks = 0;

  while (1) {
    uint32_t local = shared_state_get_local_score();
    uint32_t global = shared_state_get_global_score();
    connection_status_t status = shared_state_get_connection_status();
    bool led_flash = shared_state_take_led_flash_requested();
    bool milestone = shared_state_take_milestone_triggered();
    bool turbo_active = shared_state_get_turbo_active();

    if (turbo_active) {
      uint32_t now_ms = to_ms_since_boot(get_absolute_time());
      uint32_t until_ms = shared_state_get_turbo_until_ms();
      if ((int32_t)(until_ms - now_ms) <= 0) {
        shared_state_set_turbo_active(false);
        turbo_active = false;
      }
    }

    if (milestone) {
      milestone_glow_ticks = MILESTONE_GLOW_TICKS;
      for (int i = 0; i < 2; i++) {
        led_matrix_set_all(24, 16, 0);
        vTaskDelay(pdMS_TO_TICKS(80));
        led_clear_all();
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }

    snprintf(row1, sizeof(row1), "Node %d: %lu", NODE_ID, (unsigned long)local);
    snprintf(row2, sizeof(row2), "Global: %lu", (unsigned long)global);
    if (turbo_active) {
      snprintf(row3, sizeof(row3), "TURBO x3");
    } else {
      snprintf(row3, sizeof(row3), "%s", status_to_text(status));
    }

    display_clear();
    display_text(0, 0, "Cookie Clicker");
    display_text(2, 0, row1);
    display_text(4, 0, row2);
    display_text(6, 0, row3);
    display_show();

    uint8_t digit = (uint8_t)(local % 10u);
    if (milestone_glow_ticks > 0) {
      led_matrix_draw_number(digit, 20, 14, 0);
      milestone_glow_ticks--;
    } else if (turbo_active) {
      uint8_t r, g, b;
      wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
      led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                             (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      rainbow_phase += 7;
    } else {
      led_matrix_draw_number(digit, 8, 8, 8);
    }

    if (led_flash) {
      led_set(12, 0, 0, 15);
      vTaskDelay(pdMS_TO_TICKS(50));
      if (milestone_glow_ticks > 0) {
        led_matrix_draw_number(digit, 20, 14, 0);
      } else if (turbo_active) {
        uint8_t r, g, b;
        wheel_color((uint8_t)(rainbow_phase + (digit * 12u)), &r, &g, &b);
        led_matrix_draw_number(digit, (uint8_t)(r / 18 + 2),
                               (uint8_t)(g / 18 + 2), (uint8_t)(b / 18 + 2));
      } else {
        led_matrix_draw_number(digit, 8, 8, 8);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}

static void task_monitor(void *param) {
  (void)param;

  while (1) {
    UBaseType_t depth = uxQueueMessagesWaiting(queue_clicks);
    size_t free_heap = xPortGetFreeHeapSize();
    size_t min_heap = xPortGetMinimumEverFreeHeapSize();

    printf("[MON] queue=%lu heap=%lu min=%lu conn=%d\n", (unsigned long)depth,
           (unsigned long)free_heap, (unsigned long)min_heap,
           (int)shared_state_get_connection_status());

    vTaskDelay(pdMS_TO_TICKS(MONITOR_PERIOD_MS));
  }
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

int main(void) {
  stdio_init_all();
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  sleep_ms(1500);

  printf("\n[BOOT] ===== Clicker RP2040 - Sprint 2 =====\n");
  printf("[BOOT] FreeRTOS version: %s\n", tskKERNEL_VERSION_NUMBER);

  shared_state_init();
  lamport_init();
  button_handler_init();
  display_init();
  led_matrix_init();
  buzzer_init();

  queue_clicks = xQueueCreate(CLICK_QUEUE_LEN, sizeof(click_msg_t));
  configASSERT(queue_clicks != NULL);

  BaseType_t ok;
  ok = xTaskCreate(task_buttons, "buttons", 512, NULL, 3, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_rpc, "rpc", 4096, NULL, 2, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_display, "display", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(task_monitor, "monitor", 768, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

#ifdef STRESS_TEST
  stress_test_start();
#endif

  printf("[BOOT] Scheduler iniciado\n");
  vTaskStartScheduler();

  printf("[BOOT] ERRO FATAL: scheduler retornou\n");
  while (1) {
    tight_loop_contents();
  }
}