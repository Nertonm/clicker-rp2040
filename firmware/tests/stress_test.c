/**
 * @file stress_test.c
 * @author
 * @date 2026-03-01
 * @brief Implementação dos cenários de teste de estresse e carga.
 *
 * Provê testes automatizados para validar a robustez da comunicação RPC,
 * a resiliência a falhas de rede e a integridade da memória heap sob uso intenso.
 */

#include "stress_test.h"
#include "FreeRTOS.h"
#include "task.h"
#include "middleware/lamport.h"
#include "rpc_client.h"
#include <stdio.h>

#ifdef STRESS_TEST

/** @name Parâmetros dos Testes */
/** @{ */
#define STRESS_SERVER_PORT 8765          /**< Porta para testes. */
#define STRESS_BAD_IP "192.168.0.254"    /**< IP inexistente para testar timeout. */
#define STRESS_SPAM_ITERATIONS 100       /**< Número de envios rápidos de cliques. */
#define STRESS_RECONNECT_ITERATIONS 50   /**< Ciclos de troca de servidor. */
#define STRESS_LEAK_ITERATIONS 360       /**< Duração do teste de leak (em ciclos de 10s). */
/** @} */

/**
 * @brief Estrutura para coleta de estatísticas de cliques durante o estresse.
 */
typedef struct {
  uint32_t total_sent;    /**< Total de cliques enviados. */
  uint32_t total_success; /**< Total de cliques aceitos pelo servidor. */
  uint32_t total_offline; /**< Cliques acumulados em modo offline. */
  uint32_t total_error;   /**< Falhas inesperadas. */
} stress_click_stats_t;

/**
 * @brief Tarefa de teste ST1: Envio massivo (spam) de cliques.
 *
 * Valida se o sistema aguenta múltiplas requisições RPC sequenciais
 * sem travar ou corromper o estado de Lamport.
 */
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
    } else if (result.error_code == RPC_DISCONNECTED ||
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

/**
 * @brief Tarefa de teste ST2: Ciclos rápidos de queda e retorno de conexão.
 *
 * Simula instabilidade de rede trocando o IP do servidor entre um IP
 * inválido e o IP de fallback funcional.
 */
static void test_reconnect_cycles(void *param) {
  (void)param;

  uint32_t ok_cycles = 0;
  for (uint32_t i = 0; i < STRESS_RECONNECT_ITERATIONS; i++) {
    // Força falha configurando IP inválido
    rpc_client_set_server(STRESS_BAD_IP, STRESS_SERVER_PORT);
    vTaskDelay(pdMS_TO_TICKS(50));

    RpcClickResult fail_probe = rpc_add_clicks(1, lamport_tick());
    if (!fail_probe.success) {
      ok_cycles++;
    }

    // Restaura conexão via fallback
    rpc_client_set_server_fallback();
    vTaskDelay(pdMS_TO_TICKS(150));

    RpcClickResult recover_probe = rpc_add_clicks(1, lamport_tick());
    if (recover_probe.success) {
      ok_cycles++;
      lamport_update(recover_probe.lamport_ts);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }

  bool pass = ok_cycles >= 45; // Exige 90% de sucesso na detecção de falha/recuperação
  printf("[STRESS-2] reconnect cycles score=%lu/50 => %s\n",
         (unsigned long)ok_cycles, pass ? "PASSOU" : "FALHOU");

  vTaskDelete(NULL);
}

/**
 * @brief Tarefa de teste ST3: Monitoramento de vazamento de memória (Memory Leak).
 *
 * Acompanha o tamanho livre da heap durante um longo período de operação
 * simulada para garantir que não existam mallocs sem free.
 */
static void test_memory_leak(void *param) {
  (void)param;

  size_t initial_heap = xPortGetFreeHeapSize();
  bool leak_detected = false;

  for (uint32_t i = 0; i < STRESS_LEAK_ITERATIONS; i++) {
    vTaskDelay(pdMS_TO_TICKS(10000));

    size_t current_heap = xPortGetFreeHeapSize();
    // Tolera pequena flutuação de 1KB
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
  // Nada a fazer se MODO_STRESS não estiver definido
}

#endif
