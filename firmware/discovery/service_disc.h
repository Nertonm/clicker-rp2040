/**
 * @file service_disc.h
 * @brief Descoberta dinâmica de servidor via UDP Broadcast.
 *
 * Implementa a lógica de busca do servidor RPC na rede local,
 * enviando pacotes de broadcast e aguardando respostas de identificação.
 *
 * @author
 * @date 2026-03-01
 */

#ifndef SERVICE_DISC_H
#define SERVICE_DISC_H

#include "lwip/ip_addr.h"
#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Tenta descobrir o servidor via UDP Broadcast na rede local.
 *
 * Envia uma requisição de descoberta e aguarda por uma resposta válida
 * até o prazo limite especificado.
 *
 * @param[out] out_ip Ponteiro para armazenar o endereço IP do servidor encontrado.
 * @param[out] out_port Ponteiro para armazenar a porta de serviço do servidor.
 * @param[in] timeout_deadline Deadline absoluta (timestamp) para desistir da busca.
 *
 * @return bool Verdadeiro se um servidor foi localizado com sucesso.
 */
bool service_disc_discover(ip_addr_t *out_ip, uint16_t *out_port,
                           absolute_time_t timeout_deadline);

#endif // SERVICE_DISC_H
