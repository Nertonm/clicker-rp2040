/**
 * @file service_disc.c
 * @brief Implementação da descoberta dinâmica de serviço via UDP.
 *
 * Utiliza o protocolo de rede LWIP para enviar pacotes de broadcast
 * e receber respostas de identificação de servidores na rede local.
 *
 * @author
 * @date 2026-03-01
 */

#include "service_disc.h"
#include "lwip/udp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Suporte para fallback caso o NODE_ID não esteja definido globalmente.
 */
#ifndef NODE_ID
#define NODE_ID 0
#endif

/** @name Protocolo de Descoberta */
/** @{ */
#define DISC_PORT 9999 /**< Porta UDP fixa do servidor de descoberta. */
#define RESP_PREFIX                                                            \
  "COOKIE_SERVER:" /**< Prefixo esperado na resposta do servidor. */
#define REQ_FORMAT                                                             \
  "COOKIE_DISCOVER:NODE_ID:%d" /**< Formato do pacote de requisição. */
/** @} */

/** @name Estado Interno do Módulo (Sincronização Assíncrona) */
/** @{ */
static volatile bool g_disc_success =
    false;                       /**< Flag de sucesso atualizada no callback. */
static ip_addr_t g_disc_ip;      /**< IP do servidor capturado na resposta. */
static uint16_t g_disc_port = 0; /**< Porta do serviço capturada na resposta. */
/** @} */

/**
 * @brief Callback de recepção UDP do LWIP.
 *
 * Processa pacotes recebidos no socket de descoberta. Valida o prefixo
 * da mensagem e extrai a porta de serviço.
 *
 * @param[in] arg Argumentos de usuário (não utilizado).
 * @param[in] pcb Ponteiro para o bloco de controle UDP.
 * @param[in] p Ponteiro para o buffer do pacote (pbuf).
 * @param[in] addr Endereço IP de origem do pacote.
 * @param[in] port Porta de origem do pacote.
 */
static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port) {
  if (p == NULL)
    return;

  char buf[128];
  uint16_t copy_len =
      p->tot_len < sizeof(buf) - 1 ? p->tot_len : sizeof(buf) - 1;

  pbuf_copy_partial(p, buf, copy_len, 0);
  buf[copy_len] = '\0';

  /* Validação do prefixo e parsing da porta */
  if (strncmp(buf, RESP_PREFIX, strlen(RESP_PREFIX)) == 0) {
    int parsed_port = atoi(buf + strlen(RESP_PREFIX));
    if (parsed_port > 0 && parsed_port <= 65535) {
      ip_addr_copy(g_disc_ip, *addr);
      g_disc_port = (uint16_t)parsed_port;
      g_disc_success = true;
    }
  }

  pbuf_free(p);
}

bool service_disc_discover(ip_addr_t *out_ip, uint16_t *out_port,
                           absolute_time_t timeout_deadline) {
  g_disc_success = false;
  g_disc_port = 0; // Added for extra safety

  struct udp_pcb *pcb = udp_new();
  if (pcb == NULL) {
    printf("[DISC] Falha ao alocar UDP PCB\n");
    return false;
  }

  /* Associa a qualquer interface local */
  err_t err = udp_bind(pcb, IP_ANY_TYPE, 0);
  if (err != ERR_OK) {
    printf("[DISC] Falha no bind UDP, err=%d\n", err);
    udp_remove(pcb);
    return false;
  }

  /* Configura o callback para as respostas */
  udp_recv(pcb, udp_recv_callback, NULL);

  /* Formatação do pacote de descoberta com o ID do nó */
  char req_buf[64];
  snprintf(req_buf, sizeof(req_buf), REQ_FORMAT, NODE_ID);

  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(req_buf), PBUF_RAM);
  if (p == NULL) {
    printf("[DISC] Falha ao alocar pbuf\n");
    udp_remove(pcb);
    return false;
  }
  memcpy(p->payload, req_buf, strlen(req_buf));

  /* Envio em broadcast IPv4 na porta 9999 */
  err = udp_sendto(pcb, p, IP_ADDR_BROADCAST, DISC_PORT);
  pbuf_free(p);

  if (err != ERR_OK) {
    printf("[DISC] Falha ao enviar broadcast UDP, err=%d\n", err);
    udp_remove(pcb);
    return false;
  }

  printf("[DISC] Broadcast enviado: %s. Aguardando UDP...\n", req_buf);

  /* Loop de espera bloqueante (polling) até o timeout */
  while (absolute_time_diff_us(get_absolute_time(), timeout_deadline) > 0) {
    if (g_disc_success) {
      break;
    }
    sleep_ms(10);
  }

  /* Cleanup do socket */
  udp_recv(pcb, NULL, NULL);
  udp_remove(pcb);

  if (g_disc_success) {
    ip_addr_copy(*out_ip, g_disc_ip);
    *out_port = g_disc_port;
    printf("[DISC] Sucesso! Servidor em %s:%d\n",
           ip4addr_ntoa(ip_2_ip4(out_ip)), *out_port);
    return true;
  }

  printf("[DISC] Timeout\n");
  return false;
}
