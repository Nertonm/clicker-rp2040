#include "rpc_client.h"
#include "middleware/shared_state.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEV_SERVER_IP
#define DEV_SERVER_IP "192.168.0.10"
#endif

#define FALLBACK_PORT 8765
#define RPC_BUFFER_SIZE 512
#define RPC_FAIL_THRESHOLD 5

// Estado interno de rede
static ip_addr_t s_server_ip;
static uint16_t s_server_port = 0;
static bool s_server_has_ip = false;
static uint32_t s_rpc_fail_count = 0;

/* Helpers de Parsing Internos */

bool rpc_parse_uint32_field(const char *json, const char *key, uint32_t *out) {
  char search_pattern[64];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);

  const char *pos = strstr(json, search_pattern);
  if (!pos)
    return false;

  pos += strlen(search_pattern);

  // Pula espaços e caracteres não numéricos iniciais
  while (*pos && (*pos < '0' || *pos > '9'))
    pos++;

  if (*pos) {
    *out = (uint32_t)strtoul(pos, NULL, 10);
    return true;
  }
  return false;
}

bool rpc_has_bool_true(const char *json, const char *key) {
  char search_pattern[64];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\":true", key);
  return strstr(json, search_pattern) != NULL;
}

static int rpc_build_add_clicks_request(char *buf, size_t buf_size,
                                        uint32_t clicks, uint32_t lamport_ts) {
  return snprintf(
      buf, buf_size,
      "{\"jsonrpc\":\"2.0\",\"method\":\"add_clicks\",\"params\":{\"node_id\":"
      "\"pico_w_01\",\"clicks\":%u,\"lamport_ts\":%u},\"id\":1}\n",
      clicks, lamport_ts);
}

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
  if (!s_server_has_ip)
    return false;

  char req_buf[RPC_BUFFER_SIZE];
  char resp_buf[RPC_BUFFER_SIZE];

  // 1. Serialização
  uint32_t current_lamport = shared_state_get_lamport_ts();
  int req_len = rpc_build_add_clicks_request(req_buf, sizeof(req_buf), clicks,
                                             current_lamport);

  if (req_len < 0 || (size_t)req_len >= sizeof(req_buf)) {
    printf("[RPC] Erro: Requisição muito grande para o buffer.\n");
    return false;
  }

  printf("[RPC] Enviando: %s", req_buf);

  // 2. TODO: Substituir por envio real TCP via lwIP
  // O transporte real usará req_buf para escrita e resp_buf para leitura do
  // socket.
  snprintf(
      resp_buf, sizeof(resp_buf),
      "{\"jsonrpc\":\"2.0\",\"result\":{\"status\":\"SUCCESS\",\"accepted_"
      "clicks\":%u,\"global_score\":%u,\"node_score\":%u,\"milestone\":true,"
      "\"milestone_value\":1000,\"powerup_active\":false,\"powerup_remaining_"
      "s\":0,"
      "\"lamport_ts\":%u},\"id\":1}",
      clicks, shared_state_get_global_score() + clicks,
      shared_state_get_local_score() + clicks, current_lamport + 1);

  printf("[RPC] Recebido: %s\n", resp_buf);

  // 3. Parsing
  rpc_result_t res = {0};
  res.success = (strstr(resp_buf, "\"error\"") == NULL);

  if (res.success) {
    // Parsing numérico rigoroso via strstr + strtoul
    rpc_parse_uint32_field(resp_buf, "global_score", &res.global_score);
    rpc_parse_uint32_field(resp_buf, "node_score", &res.node_score);
    rpc_parse_uint32_field(resp_buf, "lamport_ts", &res.lamport_ts);
    rpc_parse_uint32_field(resp_buf, "milestone_value", &res.milestone_value);
    rpc_parse_uint32_field(resp_buf, "powerup_remaining_s",
                           &res.powerup_remaining_s);

    // Parsing booleano rigoroso via strstr literal
    res.milestone = rpc_has_bool_true(resp_buf, "milestone");
    res.powerup_active = rpc_has_bool_true(resp_buf, "powerup_active");

    // Sincroniza estado autoritativo vindo do servidor
    shared_state_set_global_score(res.global_score);
    shared_state_set_local_score(res.node_score);
    shared_state_set_lamport_ts(res.lamport_ts);

    if (res.milestone) {
      shared_state_set_milestone_triggered(true);
    }

    printf("[RPC] Parse OK: global=%u, local=%u, milestone=%s\n",
           res.global_score, res.node_score, res.milestone ? "YES" : "NO");
    s_rpc_fail_count = 0;
    return true;
  } else {
    s_rpc_fail_count++;
    if (s_rpc_fail_count >= RPC_FAIL_THRESHOLD)
      s_server_has_ip = false;
    return false;
  }
}

bool rpc_client_has_server(void) { return s_server_has_ip; }
