#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * Estrutura para retorno de chamadas RPC parseadas.
 */
typedef struct {
  bool success;
  uint32_t global_score;
  uint32_t node_score;
  uint32_t lamport_ts;
  bool milestone;
  uint32_t milestone_value;
  bool powerup_active;
  uint32_t powerup_remaining_s;
} rpc_result_t;

/**
 * Define o IP e a porta fornecidos (geralmente via discovery).
 */
void rpc_client_set_server(const ip_addr_t *ip, uint16_t port);

/**
 * Configura o cliente para usar o IP de fallback predefinido (DEV_SERVER_IP).
 */
void rpc_client_set_server_fallback(void);

/**
 * Envia um lote de cliques para o servidor configurado via TCP.
 * Retorna false se o servidor não estiver configurado ou em caso de falha de
 * rede.
 */
bool rpc_client_send_clicks(uint32_t clicks);

/**
 * Retorna true se houver um IP configurado para envio de pacotes.
 */
bool rpc_client_has_server(void);

/* Helpers de Parsing (Isolados para teste e clareza) */
bool rpc_parse_uint32_field(const char *json, const char *key, uint32_t *out);
bool rpc_has_bool_true(const char *json, const char *key);

#endif // RPC_CLIENT_H
