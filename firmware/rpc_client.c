/**
 * @file rpc_client.c
 * @brief Implementação do cliente JSON-RPC sobre TCP.
 *
 * Gerencia a comunicação de rede, incluindo sockets LWIP, timeouts,
 * retentativas de envio e parsing manual de respostas JSON.
 *
 * @author
 * @date 2026-03-01
 */

#include "rpc_client.h"
#include "debug_log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "lwip/sockets.h"
#include "task.h"

/* --- Helpers de diagnóstico --- */
static const char *rpc_error_name(RpcError e) {
  switch (e) {
    case RPC_OK:              return "OK";
    case RPC_TIMEOUT:         return "TIMEOUT";
    case RPC_DISCONNECTED:    return "DISCONNECTED";
    case RPC_LAMPORT_VIOLATION: return "LAMPORT_VIOLATION";
    case RPC_RATE_EXCEEDED:   return "RATE_EXCEEDED";
    case RPC_PARSE_ERROR:     return "PARSE_ERROR";
    default:                  return "UNKNOWN";
  }
}

/* --- Contadores de diagnóstico do módulo RPC --- */
static uint32_t diag_rpc_total     = 0; /* Total de chamadas rpc_call_with_retry */
static uint32_t diag_rpc_ok        = 0; /* Chamadas bem-sucedidas                */
static uint32_t diag_rpc_timeout   = 0; /* Falhas por timeout                    */
static uint32_t diag_rpc_disconnect= 0; /* Falhas por desconexão                 */
static uint32_t diag_parse_ok      = 0; /* Parse JSON bem-sucedido               */
static uint32_t diag_parse_fail    = 0; /* Parse JSON com falha                  */

/** @name Configurações de Fallback e Timeout */
/** @{ */
#ifndef FALLBACK_SERVER_IP
#define FALLBACK_SERVER_IP                                                     \
  "192.168.0.10" /**< IP padrão do servidor caso discovery falhe. */
#endif
#define FALLBACK_SERVER_PORT 8765 /**< Porta padrão do servidor RPC. */
#define RECV_TIMEOUT_MS 2000    /**< Timeout para recebimento de dados (ms). */
#define SEND_TIMEOUT_MS 2000    /**< Timeout para envio de dados (ms). */
#define CONNECT_TIMEOUT_MS 3000 /**< Timeout para conexão TCP (ms). */
#define RPC_BUFFER_SIZE 1024    /**< Tamanho do buffer de recepção. */
/** @} */

/**
 * @brief Estrutura interna de estado do cliente RPC.
 */
typedef struct {
  int sock;         /**< Descritor do socket TCP (-1 se fechado). */
  uint8_t node_id;  /**< Identificador deste nó na rede. */
  bool initialized; /**< Flag indicando se o módulo foi inicializado. */

  char server_ip[32];   /**< Endereço IP do servidor alvo. */
  uint16_t server_port; /**< Porta TCP do servidor alvo. */
  bool using_fallback;  /**< Flag indicando uso de configurações de fallback. */
} RpcState;

/**
 * @brief Instância única do estado do cliente RPC.
 */
static RpcState rpc_state = {
    .sock = -1,
    .node_id = 0,
    .initialized = false,
    .server_ip = FALLBACK_SERVER_IP,
    .server_port = FALLBACK_SERVER_PORT,
    .using_fallback = true,
};

/**
 * @brief Fecha o socket TCP e limpa o descritor no estado global.
 */
static void disconnect_socket(void) {
  if (rpc_state.sock >= 0) {
    lwip_close(rpc_state.sock);
    rpc_state.sock = -1;
    printf("[RPC] Desconectado\n");
    /* Delay para lwIP liberar recursos do socket */
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

/**
 * @brief Estabelece conexão TCP com o servidor configurado.
 *
 * Usa connect não-bloqueante com select() para garantir timeout.
 * Configura timeouts de SO_RCVTIMEO e SO_SNDTIMEO após conexão.
 *
 * @return bool Verdadeiro se a conexão foi estabelecida ou já existia.
 */
static bool connect_to_server(void) {
  if (rpc_state.sock >= 0) {
    return true;
  }

  int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    printf("[RPC] socket() falhou (errno=%d)\n", errno);
    return false;
  }

  /* Configura socket como non-blocking para connect com timeout */
  int flags = lwip_fcntl(sock, F_GETFL, 0);
  lwip_fcntl(sock, F_SETFL, flags | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(rpc_state.server_port);

  if (inet_aton(rpc_state.server_ip, &addr.sin_addr) == 0) {
    printf("[RPC] IP inválido: %s\n", rpc_state.server_ip);
    lwip_close(sock);
    return false;
  }

  int ret = lwip_connect(sock, (const struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0 && errno != EINPROGRESS) {
    /* lwIP não propaga o erro via errno no path de falha imediata.
     * Lê o erro real via getsockopt(SO_ERROR). */
    int so_err = errno;
    socklen_t so_len = sizeof(so_err);
    lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_err, &so_len);
    printf("[RPC] connect() falhou (errno=%d so_error=%d)\n", errno, so_err);
    lwip_close(sock);
    return false;
  }

  /* Aguarda conexão com timeout usando select() */
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(sock, &write_fds);

  struct timeval tv;
  tv.tv_sec = CONNECT_TIMEOUT_MS / 1000;
  tv.tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000;

  ret = lwip_select(sock + 1, NULL, &write_fds, NULL, &tv);
  if (ret <= 0) {
    printf("[RPC] connect() timeout (%d ms)\n", CONNECT_TIMEOUT_MS);
    lwip_close(sock);
    return false;
  }

  /* Verifica se a conexão teve sucesso */
  int so_error;
  socklen_t len = sizeof(so_error);
  lwip_getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
  if (so_error != 0) {
    printf("[RPC] connect() erro assíncrono (errno=%d)\n", so_error);
    lwip_close(sock);
    return false;
  }

  /* Restaura modo blocking e configura timeouts de I/O */
  lwip_fcntl(sock, F_SETFL, flags);

  struct timeval timeout;
  timeout.tv_sec = RECV_TIMEOUT_MS / 1000;
  timeout.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
  lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  timeout.tv_sec = SEND_TIMEOUT_MS / 1000;
  timeout.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
  lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  /* Habilita TCP keepalive para detectar conexões mortas */
  int keepalive = 1;
  lwip_setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));

  rpc_state.sock = sock;
  printf("[RPC] Conectado ao servidor %s:%u\n", rpc_state.server_ip,
         (unsigned)rpc_state.server_port);
  return true;
}

/**
 * @brief Garante o envio de todo o buffer através do socket.
 *
 * @param[in] sock Descritor do socket.
 * @param[in] data Ponteiro para os dados.
 * @param[in] len Tamanho total a enviar.
 * @return bool Verdadeiro se todo o conteúdo foi enviado.
 */
static bool send_all(int sock, const char *data, size_t len) {
  size_t total_sent = 0;
  while (total_sent < len) {
    int sent = lwip_send(sock, data + total_sent, len - total_sent, 0);
    if (sent <= 0) {
      return false;
    }
    total_sent += (size_t)sent;
  }
  return true;
}

/**
 * @brief Envia uma requisição e aguarda a resposta correspondente.
 *
 * @param[in] request String contendo o JSON de requisição.
 * @param[out] response Buffer para armazenar a resposta recebida.
 * @param[in] response_size Tamanho máximo do buffer de resposta.
 * @return bool Verdadeiro se a transação foi concluída com sucesso.
 */
static bool send_and_receive(const char *request, char *response,
                             size_t response_size) {
  if (rpc_state.sock < 0 || response_size == 0) {
    return false;
  }

  size_t req_len = strlen(request);
  if (!send_all(rpc_state.sock, request, req_len)) {
    printf("[RPC] send() falhou (errno=%d)\n", errno);
    disconnect_socket();
    return false;
  }

  /* Usa select() para garantir timeout no recv (SO_RCVTIMEO não é confiável) */
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(rpc_state.sock, &read_fds);

  struct timeval tv;
  tv.tv_sec = RECV_TIMEOUT_MS / 1000;
  tv.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;

  int sel = lwip_select(rpc_state.sock + 1, &read_fds, NULL, NULL, &tv);
  if (sel <= 0) {
    printf("[RPC] recv timeout (select=%d)\n", sel);
    disconnect_socket();
    return false;
  }

  int n = lwip_recv(rpc_state.sock, response, response_size - 1, 0);
  if (n <= 0) {
    printf("[RPC] recv()=%d errno=%d\n", n, errno);
    disconnect_socket();
    return false;
  }

  response[n] = '\0';
  return true;
}

/** @brief Tabela de backoff exponencial para retentativas (ms). */
static const uint32_t BACKOFF_MS[] = {100, 200, 400};

/**
 * @brief Realiza uma única chamada RPC com conexão persistente.
 *
 * Mantém a conexão TCP aberta para chamadas subsequentes. Só reconecta
 * se a conexão falhar ou não existir.
 *
 * @param[in] request Requisição JSON.
 * @param[out] response Buffer de resposta.
 * @param[in] response_size Tamanho do buffer.
 * @return RpcError Código de erro da operação.
 */
static RpcError rpc_call_once(const char *request, char *response,
                              size_t response_size) {
  /* Tenta usar conexão existente ou cria nova */
  if (!connect_to_server()) {
    printf("[RPC] connect falhou\n");
    return RPC_DISCONNECTED;
  }

  if (!send_and_receive(request, response, response_size)) {
    printf("[RPC] send/recv falhou, reconectando...\n");
    /* Conexão falhou - fecha e tenta reconectar uma vez */
    disconnect_socket();
    if (!connect_to_server()) {
      printf("[RPC] reconexão falhou\n");
      return RPC_DISCONNECTED;
    }
    /* Segunda tentativa após reconexão */
    if (!send_and_receive(request, response, response_size)) {
      printf("[RPC] segunda tentativa falhou\n");
      disconnect_socket();
      if (errno == EAGAIN || errno == ETIMEDOUT) {
        return RPC_TIMEOUT;
      }
      return RPC_DISCONNECTED;
    }
  }

  /* Mantém conexão aberta para próxima chamada */
  return RPC_OK;
}

/**
 * @brief Orquestrador de chamadas RPC com lógica de re-tentativa automática.
 *
 * Tenta realizar a chamada até 3 vezes em caso de falha de rede.
 *
 * @param[in] request Requisição JSON.
 * @param[out] response Buffer de resposta.
 * @param[in] response_size Tamanho do buffer.
 * @param[out] out_err Ponteiro opcional para erro detalhado.
 * @return bool Verdadeiro se alguma das tentativas teve sucesso.
 */
static bool rpc_call_with_retry(const char *request, char *response,
                                size_t response_size, RpcError *out_err) {
  RpcError last_err = RPC_DISCONNECTED;
  DIAG_CNT_INC(diag_rpc_total);

  for (int attempt = 1; attempt <= 3; attempt++) {
    last_err = rpc_call_once(request, response, response_size);
    if (last_err == RPC_OK) {
      DIAG_CNT_INC(diag_rpc_ok);
      if (out_err)
        *out_err = RPC_OK;
      LOG_VERBOSE("[RPC]", "chamada ok tentativa=%d total=%lu ok=%lu",
                  attempt, (unsigned long)diag_rpc_total,
                  (unsigned long)diag_rpc_ok);
      return true;
    }

    if (last_err == RPC_TIMEOUT) {
      DIAG_CNT_INC(diag_rpc_timeout);
    } else {
      DIAG_CNT_INC(diag_rpc_disconnect);
    }
    LOG_ERROR("[RPC]", "FALHA tentativa=%d/3 err=%s timeouts=%lu disconnects=%lu",
              attempt, rpc_error_name(last_err),
              (unsigned long)diag_rpc_timeout,
              (unsigned long)diag_rpc_disconnect);

    if (attempt < 3) {
      vTaskDelay(pdMS_TO_TICKS(BACKOFF_MS[attempt - 1]));
    }
  }
  if (out_err)
    *out_err = last_err;
  return false;
}

/* --- Funções de Construção de JSON (Manuais) --- */

static void build_register_node_json(char *buffer, size_t size,
                                     uint8_t node_id) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"register_node\","
           "\"params\":{\"node_id\":%d},\"id\":2}\n",
           node_id);
}

static void build_add_clicks_json(char *buffer, size_t size, int clicks,
                                  uint32_t lamport_ts) {
  snprintf(
      buffer, size,
      "{\"jsonrpc\":\"2.0\",\"method\":\"add_clicks\","
      "\"params\":{\"node_id\":%d,\"clicks\":%d,\"lamport_ts\":%u},\"id\":1}\n",
      rpc_state.node_id, clicks, (unsigned int)lamport_ts);
}

static void build_activate_powerup_json(char *buffer, size_t size) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"activate_powerup\","
           "\"params\":{\"node_id\":%d},\"id\":3}\n",
           rpc_state.node_id);
}

static void build_get_scores_json(char *buffer, size_t size) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"get_scores\","
           "\"params\":{},\"id\":4}\n");
}

static void build_sync_offline_json(char *buffer, size_t size,
                                    int accumulated_clicks,
                                    uint32_t lamport_ts) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"sync_offline\","
           "\"params\":{\"node_id\":%d,\"accumulated_clicks\":%d,\"lamport_"
           "ts\":%u},\"id\":5}\n",
           rpc_state.node_id, accumulated_clicks, (unsigned int)lamport_ts);
}

/* --- Funções de Parsing de JSON (Simplificadas) --- */

/**
 * @brief Verifica se uma substring existe no JSON recebido.
 * @param json String JSON original.
 * @param substring Termo de busca.
 * @return bool Verdadeiro se encontrado.
 */
static bool json_contains(const char *json, const char *substring) {
  return strstr(json, substring) != NULL;
}

/**
 * @brief Extrai um valor inteiro de um campo JSON específico.
 *
 * @param[in] json String JSON original.
 * @param[in] key Nome da chave (ex: "lamport_ts").
 * @param[out] out_value Ponteiro para armazenar o valor extraído.
 * @return bool Verdadeiro se a chave foi encontrada e o valor parseado.
 */
static bool json_get_int(const char *json, const char *key, int *out_value) {
  char search_pattern[64];
  snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", key);

  const char *pos = strstr(json, search_pattern);
  if (!pos) {
    return false;
  }

  pos += strlen(search_pattern);

  while (*pos == ' ' || *pos == '\t') {
    pos++;
  }

  if (*pos != '-' && (*pos < '0' || *pos > '9')) {
    printf("[RPC] WARN: Campo '%s' não é inteiro (valor: %.20s)\n", key, pos);
    return false;
  }

  int parsed;
  if (sscanf(pos, "%d", &parsed) != 1) {
    printf("[RPC] WARN: Falha ao parsear campo '%s'\n", key);
    return false;
  }

  *out_value = parsed;
  return true;
}

static RpcSimpleResult parse_register_node_response(const char *json) {
  RpcSimpleResult result = {0};

  if (json_contains(json, "\"error\"")) {
    result.success = false;
    result.error_code = RPC_PARSE_ERROR;
    return result;
  }

  result.success = json_contains(json, "\"result\"");
  result.error_code = result.success ? RPC_OK : RPC_PARSE_ERROR;
  return result;
}

static RpcClickResult parse_add_clicks_response(const char *json) {
  RpcClickResult result = {0};

  if (json_contains(json, "\"error\"")) {
    result.success = false;

    if (json_contains(json, "LAMPORT_VIOLATION")) {
      result.error_code = RPC_LAMPORT_VIOLATION;
      int temp_lamport;
      json_get_int(json, "lamport_ts", &temp_lamport);
      result.lamport_ts = (uint32_t)temp_lamport;
      LOG_ERROR("[RPC]", "parse: LAMPORT_VIOLATION server_ts=%lu",
                (unsigned long)result.lamport_ts);
    } else if (json_contains(json, "RATE_EXCEEDED")) {
      result.error_code = RPC_RATE_EXCEEDED;
      json_get_int(json, "accepted_partial", &result.accepted_clicks);
      LOG_NORMAL("[RPC]", "parse: RATE_EXCEEDED aceitos=%d", result.accepted_clicks);
    } else {
      result.error_code = RPC_PARSE_ERROR;
      DIAG_CNT_INC(diag_parse_fail);
      LOG_ERROR("[RPC]", "parse: erro desconhecido parse_fails=%lu raw=%.80s",
                (unsigned long)diag_parse_fail, json);
    }
    return result;
  }

  result.success = true;
  result.error_code = RPC_OK;

  bool ok_gs = json_get_int(json, "global_score", &result.global_score);
  bool ok_ls = json_get_int(json, "node_score",   &result.local_score);
  int temp_lamport = 0;
  bool ok_ts = json_get_int(json, "lamport_ts",   &temp_lamport);
  result.lamport_ts = (uint32_t)temp_lamport;
  json_get_int(json, "clicks", &result.accepted_clicks);

  if (!ok_gs || !ok_ls || !ok_ts) {
    DIAG_CNT_INC(diag_parse_fail);
    LOG_ERROR("[RPC]", "parse: campos ausentes gs=%d ls=%d ts=%d parse_fails=%lu raw=%.80s",
              ok_gs, ok_ls, ok_ts,
              (unsigned long)diag_parse_fail, json);
    result.success = false;
    result.error_code = RPC_PARSE_ERROR;
    return result;
  }

  result.milestone_triggered = json_contains(json, "\"milestone\":true");
  if (result.milestone_triggered) {
    json_get_int(json, "milestone_value", &result.milestone_value);
    LOG_NORMAL("[RPC]", "parse: MILESTONE milestone_value=%d", result.milestone_value);
  }

  DIAG_CNT_INC(diag_parse_ok);
  LOG_VERBOSE("[RPC]", "parse add_clicks: global=%d local=%d ts=%lu aceitos=%d",
              result.global_score, result.local_score,
              (unsigned long)result.lamport_ts, result.accepted_clicks);
  return result;
}

static RpcPowerupResult parse_activate_powerup_response(const char *json) {
  RpcPowerupResult result = {0};

  if (json_contains(json, "\"error\"")) {
    if (json_contains(json, "ALREADY_ACTIVE")) {
      result.success = true;
      result.error_code = RPC_OK;
      result.powerup_remaining_s = 10;
    } else {
      result.success = false;
      result.error_code = RPC_PARSE_ERROR;
    }
    return result;
  }

  result.success = true;
  result.error_code = RPC_OK;

  if (!json_get_int(json, "time_remaining", &result.powerup_remaining_s)) {
    result.powerup_remaining_s = 10;
  }

  return result;
}

static RpcScoreResult parse_get_scores_response(const char *json) {
  RpcScoreResult result = {0};

  if (json_contains(json, "\"error\"")) {
    result.success = false;
    result.error_code = RPC_PARSE_ERROR;
    return result;
  }

  result.success = true;
  result.error_code = RPC_OK;

  json_get_int(json, "global_score", &result.global_score);

  const char *nodes_start = strstr(json, "\"nodes\"");
  if (nodes_start) {
    const char *pos = nodes_start;
    for (int i = 0; i < 3; i++) {
      pos = strstr(pos, "\"local_score\"");
      if (pos) {
        pos += strlen("\"local_score\":");
        result.node_scores[i] = atoi(pos);
        pos++;
      }
    }
  }

  return result;
}

/* --- Implementação da API Pública --- */

void rpc_client_set_server(const char *ip_str, uint16_t port) {
  if (!ip_str || ip_str[0] == '\0') {
    printf("[RPC] ERRO: IP string inválido\n");
    return;
  }

  disconnect_socket();

  snprintf(rpc_state.server_ip, sizeof(rpc_state.server_ip), "%s", ip_str);
  rpc_state.server_port = port;
  rpc_state.using_fallback = false;

  printf("[RPC] Servidor configurado: %s:%u\n", rpc_state.server_ip,
         (unsigned)rpc_state.server_port);
}

void rpc_client_set_server_fallback(void) {
  rpc_client_set_server(FALLBACK_SERVER_IP, FALLBACK_SERVER_PORT);
  rpc_state.using_fallback = true;
}

RpcSimpleResult rpc_init(void) {
  if (rpc_state.initialized) {
    return (RpcSimpleResult){.success = true, .error_code = RPC_OK};
  }

  memset(&rpc_state, 0, sizeof(rpc_state));
  rpc_state.sock = -1;
  rpc_state.server_port = FALLBACK_SERVER_PORT;
  snprintf(rpc_state.server_ip, sizeof(rpc_state.server_ip), "%s",
           FALLBACK_SERVER_IP);
  rpc_state.using_fallback = true;
  rpc_state.initialized = true;

  printf("Cliente inicializado\n");
  return (RpcSimpleResult){.success = true, .error_code = RPC_OK};
}

RpcSimpleResult rpc_register_node(uint8_t node_id) {
  RpcSimpleResult result = {0};
  rpc_state.node_id = node_id;

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_register_node_json(request, sizeof(request), node_id);

  RpcError err;
  if (!rpc_call_with_retry(request, response, sizeof(response), &err)) {
    result.success = false;
    result.error_code = err;
    return result;
  }

  return parse_register_node_response(response);
}

RpcClickResult rpc_add_clicks(int clicks, uint32_t lamport_ts) {
  RpcClickResult result = {0};

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_add_clicks_json(request, sizeof(request), clicks, lamport_ts);

  LOG_VERBOSE("[RPC]", "add_clicks: node=%d clicks=%d lamport=%lu",
              (int)rpc_state.node_id, clicks, (unsigned long)lamport_ts);

  RpcError err;
  if (!rpc_call_with_retry(request, response, sizeof(response), &err)) {
    result.success = false;
    result.error_code = err;
    LOG_ERROR("[RPC]", "add_clicks FALHA err=%d total=%lu ok=%lu",
              (int)err, (unsigned long)diag_rpc_total, (unsigned long)diag_rpc_ok);
    return result;
  }

  result = parse_add_clicks_response(response);
  if (result.success) {
    LOG_NORMAL("[RPC]", "add_clicks OK global=%d local=%d ts=%lu ok=%lu/%lu",
               result.global_score, result.local_score,
               (unsigned long)result.lamport_ts,
               (unsigned long)diag_rpc_ok, (unsigned long)diag_rpc_total);
  }
  return result;
}

RpcPowerupResult rpc_activate_powerup(void) {
  RpcPowerupResult result = {0};

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_activate_powerup_json(request, sizeof(request));

  RpcError err;
  if (!rpc_call_with_retry(request, response, sizeof(response), &err)) {
    result.success = false;
    result.error_code = err;
    return result;
  }

  return parse_activate_powerup_response(response);
}

RpcScoreResult rpc_get_scores(void) {
  RpcScoreResult result = {0};

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_get_scores_json(request, sizeof(request));

  RpcError err;
  if (!rpc_call_with_retry(request, response, sizeof(response), &err)) {
    result.success = false;
    result.error_code = err;
    return result;
  }

  return parse_get_scores_response(response);
}

RpcClickResult rpc_sync_offline(int accumulated_clicks, uint32_t lamport_ts) {
  RpcClickResult result = {0};

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_sync_offline_json(request, sizeof(request), accumulated_clicks,
                          lamport_ts);

  LOG_NORMAL("[SYNC]", "sync_offline: node=%d accumulated=%d lamport=%lu",
             (int)rpc_state.node_id, accumulated_clicks, (unsigned long)lamport_ts);

  RpcError err;
  if (!rpc_call_with_retry(request, response, sizeof(response), &err)) {
    result.success = false;
    result.error_code = err;
    LOG_ERROR("[SYNC]", "sync_offline FALHA err=%d", (int)err);
    return result;
  }

  result = parse_add_clicks_response(response);
  if (result.success) {
    LOG_NORMAL("[SYNC]", "sync_offline OK global=%d local=%d aceitos=%d ts=%lu",
               result.global_score, result.local_score,
               result.accepted_clicks,
               (unsigned long)result.lamport_ts);
  } else {
    LOG_ERROR("[SYNC]", "sync_offline parse FALHA err=%d", (int)result.error_code);
  }
  return result;
}

bool rpc_is_connected(void) { return rpc_state.sock >= 0; }

void rpc_print_diagnostics(void) {
  LOG_NORMAL("[RPC]", "[STATS] total=%lu ok=%lu timeouts=%lu disconnects=%lu parse_ok=%lu parse_fail=%lu connected=%d",
             (unsigned long)diag_rpc_total,
             (unsigned long)diag_rpc_ok,
             (unsigned long)diag_rpc_timeout,
             (unsigned long)diag_rpc_disconnect,
             (unsigned long)diag_parse_ok,
             (unsigned long)diag_parse_fail,
             (int)(rpc_state.sock >= 0));
}
