#include "rpc_client.h"
#include <stdio.h>

#ifndef DEV_SERVER_IP
#define DEV_SERVER_IP "192.168.0.10"
#endif

// Porta de fallback padrão para RPC local
#define FALLBACK_PORT 8080

// Estado interno de rede: isolado da aplicação principal
static ip_addr_t s_server_ip;
static uint16_t s_server_port = 0;
static bool s_server_has_ip = false;
static uint32_t s_rpc_fail_count = 0;
#define RPC_FAIL_THRESHOLD 5

void rpc_client_set_server(const ip_addr_t *ip, uint16_t port) {
  if (ip == NULL)
    return;

  ip_addr_copy(s_server_ip, *ip);
  s_server_port = port;
  s_server_has_ip = true;
  s_rpc_fail_count = 0;

  printf("[RPC] Endpoint oficial definido: %s:%d\n",
         ip4addr_ntoa(ip_2_ip4(&s_server_ip)), s_server_port);
}

void rpc_client_set_server_fallback(void) {
  ip4addr_aton(DEV_SERVER_IP, ip_2_ip4(&s_server_ip));
  s_server_port = FALLBACK_PORT;
  s_server_has_ip = true;
  s_rpc_fail_count = 0;

  printf("[RPC] Endpoint de FALLBACK definido: %s:%d\n", DEV_SERVER_IP,
         FALLBACK_PORT);
}

bool rpc_client_send_clicks(uint32_t clicks) {
  if (!s_server_has_ip) {
    printf("[RPC] Falha: sem servidor configurado (endpoint inválido).\n");
    return false;
  }

  printf("[RPC] Simulando envio TCP/HTTP de %u cliques para %s:%d...\n", clicks,
         ip4addr_ntoa(ip_2_ip4(&s_server_ip)), s_server_port);

  // SIMULAÇÃO DA LÓGICA DE ENVIO
  // Temporariamente extraímos uma variável 'ok' para plugar
  // a aleatoriedade de falha que estava na main.c stub
  static uint32_t attempt = 0;
  attempt++;
  bool ok = (attempt % 3 != 0); // Falha a cada 3 tentativas

  if (ok) {
    s_rpc_fail_count = 0;
    printf("[RPC] Success: sent %u clicks\n", clicks);
    return true;
  } else {
    s_rpc_fail_count++;
    printf("[RPC] Falha RPC consecutiva #%u.\n", s_rpc_fail_count);

    if (s_rpc_fail_count >= RPC_FAIL_THRESHOLD) {
      printf("[RPC] Limite de falhas atingido. Invalidando servidor atual.\n");
      s_server_has_ip = false;
    }
    return false;
  }
}

bool rpc_client_has_server(void) { return s_server_has_ip; }
