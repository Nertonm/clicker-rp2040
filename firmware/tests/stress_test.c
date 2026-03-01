#include "stress_test.h"

#include "FreeRTOS.h"
#include "task.h"

#include "middleware/lamport.h"
#include "rpc_client.h"

#include <stdio.h>

#ifdef STRESS_TEST

#define STRESS_SERVER_PORT 8765
#define STRESS_BAD_IP "192.168.0.254"
#define STRESS_SPAM_ITERATIONS 100
#define STRESS_RECONNECT_ITERATIONS 50
#define STRESS_LEAK_ITERATIONS 360

typedef struct {
  uint32_t total_sent;
  uint32_t total_success;
  uint32_t total_offline;
  uint32_t total_error;
} stress_click_stats_t;

static void test_click_spam(void *param) {
  (void)param;

  stress_click_stats_t stats = {0};
  for (uint32_t i = 0; i < STRESS_SPAM_ITERATIONS; i++) {
    int lamport_ts = lamport_tick();
    RpcClickResult result = rpc_add_clicks(10, lamport_ts);
    stats.total_sent += 10;

    if (result.success) {
      stats.total_success += (uint32_t)result.accepted_clicks;
      lamport_update(result.lamport_ts);
    } else if (result.error_code == RPC_OFFLINE_QUEUED ||
               result.error_code == RPC_DISCONNECTED ||
               result.error_code == RPC_TIMEOUT) {
      stats.total_offline += 10;
    } else {
      stats.total_error += 10;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  bool pass = (stats.total_error == 0) &&
              ((stats.total_success + stats.total_offline) == stats.total_sent);

  printf("[STRESS-1] sent=%lu ok=%lu offline=%lu error=%lu => %s\n",
         (unsigned long)stats.total_sent, (unsigned long)stats.total_success,
         (unsigned long)stats.total_offline, (unsigned long)stats.total_error,
         pass ? "PASSOU" : "FALHOU");

  vTaskDelete(NULL);
}

static void test_reconnect_cycles(void *param) {
  (void)param;

  uint32_t ok_cycles = 0;
  for (uint32_t i = 0; i < STRESS_RECONNECT_ITERATIONS; i++) {
    rpc_client_set_server(STRESS_BAD_IP, STRESS_SERVER_PORT);
    vTaskDelay(pdMS_TO_TICKS(50));

    RpcClickResult fail_probe = rpc_add_clicks(1, lamport_tick());
    if (!fail_probe.success) {
      ok_cycles++;
    }

    rpc_client_set_server_fallback();
    vTaskDelay(pdMS_TO_TICKS(150));

    RpcClickResult recover_probe = rpc_add_clicks(1, lamport_tick());
    if (recover_probe.success) {
      ok_cycles++;
      lamport_update(recover_probe.lamport_ts);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  bool pass = ok_cycles >= 45;
  printf("[STRESS-2] reconnect cycles score=%lu/50 => %s\n",
         (unsigned long)ok_cycles, pass ? "PASSOU" : "FALHOU");

  vTaskDelete(NULL);
}

static void test_memory_leak(void *param) {
  (void)param;

  size_t initial_heap = xPortGetFreeHeapSize();
  bool leak_detected = false;

  for (uint32_t i = 0; i < STRESS_LEAK_ITERATIONS; i++) {
    vTaskDelay(pdMS_TO_TICKS(10000));

    size_t current_heap = xPortGetFreeHeapSize();
    if (current_heap + 1024 < initial_heap) {
      leak_detected = true;
    }

    printf("[STRESS-3] t=%lu min heap=%lu current=%lu\n",
           (unsigned long)((i + 1) * 10),
           (unsigned long)xPortGetMinimumEverFreeHeapSize(),
           (unsigned long)current_heap);
  }

  size_t final_heap = xPortGetFreeHeapSize();
  long delta = (long)initial_heap - (long)final_heap;

  bool pass = !leak_detected && (delta < 1024);
  printf("[STRESS-3] initial=%lu final=%lu delta=%ld => %s\n",
         (unsigned long)initial_heap, (unsigned long)final_heap, delta,
         pass ? "PASSOU" : "FALHOU");

  vTaskDelete(NULL);
}

void stress_test_start(void) {
  printf("\n========================================\n");
  printf("  MODO STRESS TEST ATIVO\n");
  printf("  Aguardando servidor em fallback:8765\n");
  printf("========================================\n\n");

  BaseType_t ok;
  ok = xTaskCreate(test_click_spam, "ST1", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(test_reconnect_cycles, "ST2", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);

  ok = xTaskCreate(test_memory_leak, "ST3", 1024, NULL, 1, NULL);
  configASSERT(ok == pdPASS);
}

#else

void stress_test_start(void) {
}

#endif
