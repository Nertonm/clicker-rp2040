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
static uint8_t s_consec_failures = 0;

void rpc_client_set_server(const ip_addr_t *ip, uint16_t port) {
  if (ip == NULL)
    return;

  ip_addr_copy(s_server_ip, *ip);
  s_server_port = port;
  s_server_has_ip = true;
  s_consec_failures = 0;

  printf("[RPC] Endpoint oficial definido: %s:%d\n",
         ip4addr_ntoa(ip_2_ip4(&s_server_ip)), s_server_port);
}

void rpc_client_set_server_fallback(void) {
  ip4addr_aton(DEV_SERVER_IP, ip_2_ip4(&s_server_ip));
  s_server_port = FALLBACK_PORT;
  s_server_has_ip = true;
  s_consec_failures = 0;

  printf("[RPC] Endpoint de FALLBACK definido: %s:%d\n", DEV_SERVER_IP,
         FALLBACK_PORT);
}

bool rpc_client_send_clicks(uint32_t clicks) {
  if (!s_server_has_ip) {
    printf("[RPC] Falha: Tentativa de envio sem um IP de servidor (Discovery "
           "pendente?).\n");
    return false;
  }

  printf("[RPC] Simulando envio TCP/HTTP de %u cliques para %s:%d...\n", clicks,
         ip4addr_ntoa(ip_2_ip4(&s_server_ip)), s_server_port);

  // No futuro aqui entra a criação de raw TCP PCB ou envio HTTP direto
  // Por enquanto, o driver de interface para o MAIN irá utilizar a variável
  // booleana externa enviorpc.
  return true;
}

// Interfece de falha explícita para o main comunicar ao rpc (pois no stub não
// temos callback real)
void rpc_client_register_failure(void) {
  s_consec_failures++;
  printf("[RPC] Falha registrada (%u/5)\n", s_consec_failures);

  if (s_consec_failures >= 5) {
    printf("[RPC] Limite de falhas atingido. Inviabilizando endpoint atual.\n");
    s_server_has_ip = false;
  }
}

void rpc_client_register_success(void) { s_consec_failures = 0; }

bool rpc_client_has_server(void) { return s_server_has_ip; }
