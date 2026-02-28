#include "service_disc.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MACRO para suportar fallback caso NODE_ID não exista
#ifndef NODE_ID
#define NODE_ID 0
#endif

#define DISC_PORT 9999
#define RESP_PREFIX "COOKIE_SERVER:"
#define REQ_FORMAT "COOKIE_DISCOVER:NODE_ID:%d"

// Estado compartilhado internamente no módulo de discovery (assíncrono ->
// síncrono)
static volatile bool g_disc_success = false;
static ip_addr_t g_disc_ip;
static uint16_t g_disc_port = 0;

static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port) {
  if (p == NULL)
    return;

  // Buffer pra não estourar lendo do payload
  char buf[128];
  uint16_t copy_len =
      p->tot_len < sizeof(buf) - 1 ? p->tot_len : sizeof(buf) - 1;

  pbuf_copy_partial(p, buf, copy_len, 0);
  buf[copy_len] = '\0';

  // Checa se tem a string esperada
  if (strncmp(buf, RESP_PREFIX, strlen(RESP_PREFIX)) == 0) {
    // COOKIE_SERVER:<porta>
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

  struct udp_pcb *pcb = udp_new();
  if (pcb == NULL) {
    printf("[DISC] Falha ao alocar UDP PCB\n");
    return false;
  }

  // Ouve por respostas em qualquer porta
  err_t err = udp_bind(pcb, IP_ANY_TYPE, 0);
  if (err != ERR_OK) {
    printf("[DISC] Falha no bind UDP, err=%d\n", err);
    udp_remove(pcb);
    return false;
  }

  udp_recv(pcb, udp_recv_callback, NULL);

  // Formata pacote de broadcast
  char req_buf[64];
  snprintf(req_buf, sizeof(req_buf), REQ_FORMAT, NODE_ID);

  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, strlen(req_buf), PBUF_RAM);
  if (p == NULL) {
    printf("[DISC] Falha ao alocar pbuf\n");
    udp_remove(pcb);
    return false;
  }
  memcpy(p->payload, req_buf, strlen(req_buf));

  // Envia broadcast IPv4 255.255.255.255 na porta fixa 9999
  err = udp_sendto(pcb, p, IP_ADDR_BROADCAST, DISC_PORT);
  pbuf_free(p);

  if (err != ERR_OK) {
    printf("[DISC] Falha ao enviar broadcast UDP, err=%d\n", err);
    udp_remove(pcb);
    return false;
  }

  printf("[DISC] Broadcast enviado: %s. Aguardando UDP...\n", req_buf);

  // Poll bloqueante usando tempo absoluto
  while (absolute_time_diff_us(get_absolute_time(), timeout_deadline) > 0) {
    cyw43_arch_poll(); // Mandatório em NO_SYS=1

    if (g_disc_success) {
      break;
    }

    sleep_ms(10); // Evita espancar a CPU
  }

  // Cleanup pcb
  udp_recv(pcb, NULL, NULL);
  udp_remove(pcb);

  if (g_disc_success) {
    ip_addr_copy(*out_ip, g_disc_ip);
    *out_port = g_disc_port;
    // Print convertendo do raw
    printf("[DISC] Sucesso! Servidor em %s:%d\n",
           ip4addr_ntoa(ip_2_ip4(out_ip)), *out_port);
    return true;
  }

  printf("[DISC] Timeout\n");
  return false;
}
