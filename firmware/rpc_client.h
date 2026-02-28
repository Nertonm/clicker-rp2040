#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

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
 * Útil para saber se o client invalidou espontaneamente o endpoint.
 */
bool rpc_client_has_server(void);

#endif // RPC_CLIENT_H
