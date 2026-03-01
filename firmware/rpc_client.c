#include "rpc_client.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "pico/time.h"

#ifndef FALLBACK_SERVER_IP
#define FALLBACK_SERVER_IP "192.168.0.10"
#endif
#define FALLBACK_SERVER_PORT 8765
#define RECV_TIMEOUT_MS 5000
#define SEND_TIMEOUT_MS 5000
#define RECONNECT_INTERVAL_MS 2000
#define RPC_BUFFER_SIZE 1024

typedef struct {
  int sock;
  uint8_t node_id;
  bool initialized;

  char server_ip[32];
  uint16_t server_port;
  bool using_fallback;

  struct {
    int accumulated_clicks;
    int last_lamport;
  } offline_queue;

  uint32_t last_reconnect_attempt_ms;
} RpcState;

static RpcState rpc_state = {
    .sock = -1,
    .node_id = 0,
    .initialized = false,
    .server_ip = FALLBACK_SERVER_IP,
    .server_port = FALLBACK_SERVER_PORT,
    .using_fallback = true,
    .offline_queue = {0, 0},
    .last_reconnect_attempt_ms = 0,
};

static uint32_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }

static void disconnect_socket(void) {
  if (rpc_state.sock >= 0) {
    lwip_close(rpc_state.sock);
    rpc_state.sock = -1;
    printf("[RPC] Desconectado\n");
  }
}

static bool connect_to_server(void) {
  if (rpc_state.sock >= 0) {
    return true;
  }

  int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    printf("[RPC] socket() falhou (errno=%d)\n", errno);
    return false;
  }

  struct timeval timeout;
  timeout.tv_sec = RECV_TIMEOUT_MS / 1000;
  timeout.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;
  lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  timeout.tv_sec = SEND_TIMEOUT_MS / 1000;
  timeout.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
  lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(rpc_state.server_port);

  if (inet_aton(rpc_state.server_ip, &addr.sin_addr) == 0) {
    printf("[RPC] IP inválido: %s\n", rpc_state.server_ip);
    lwip_close(sock);
    return false;
  }

  if (lwip_connect(sock, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
    printf("[RPC] connect() falhou (errno=%d)\n", errno);
    lwip_close(sock);
    return false;
  }

  rpc_state.sock = sock;
  printf("[RPC] Conectado ao servidor %s:%u\n", rpc_state.server_ip,
         (unsigned)rpc_state.server_port);
  return true;
}

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

  int n = lwip_recv(rpc_state.sock, response, response_size - 1, 0);
  if (n <= 0) {
    printf("[RPC] recv() falhou/timeout (errno=%d)\n", errno);
    disconnect_socket();
    return false;
  }

  response[n] = '\0';
  return true;
}

static void build_register_node_json(char *buffer, size_t size,
                                     uint8_t node_id) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"register_node\","
           "\"params\":{\"node_id\":%d},\"id\":2}\n",
           node_id);
}

static void build_add_clicks_json(char *buffer, size_t size, int clicks,
                                  int lamport_ts) {
  snprintf(
      buffer, size,
      "{\"jsonrpc\":\"2.0\",\"method\":\"add_clicks\","
      "\"params\":{\"node_id\":%d,\"clicks\":%d,\"lamport_ts\":%d},\"id\":1}\n",
      rpc_state.node_id, clicks, lamport_ts);
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
                                    int accumulated_clicks, int lamport_ts) {
  snprintf(buffer, size,
           "{\"jsonrpc\":\"2.0\",\"method\":\"sync_offline\","
           "\"params\":{\"node_id\":%d,\"accumulated_clicks\":%d,\"lamport_"
           "ts\":%d},\"id\":5}\n",
           rpc_state.node_id, accumulated_clicks, lamport_ts);
}

static bool json_contains(const char *json, const char *substring) {
  return strstr(json, substring) != NULL;
}

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
      json_get_int(json, "lamport_ts", &result.lamport_ts);
    } else if (json_contains(json, "RATE_EXCEEDED")) {
      result.error_code = RPC_RATE_EXCEEDED;
      json_get_int(json, "accepted_partial", &result.accepted_clicks);
    } else {
      result.error_code = RPC_PARSE_ERROR;
    }
    return result;
  }

  result.success = true;
  result.error_code = RPC_OK;

  json_get_int(json, "global_score", &result.global_score);
  json_get_int(json, "node_score", &result.local_score);
  json_get_int(json, "lamport_ts", &result.lamport_ts);
  json_get_int(json, "clicks", &result.accepted_clicks);

  result.milestone_triggered = json_contains(json, "\"milestone\":true");
  if (result.milestone_triggered) {
    json_get_int(json, "milestone_value", &result.milestone_value);
  }

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
  rpc_state.last_reconnect_attempt_ms = 0;

  printf("[RPC] Cliente inicializado\n");
  return (RpcSimpleResult){.success = true, .error_code = RPC_OK};
}

RpcSimpleResult rpc_register_node(uint8_t node_id) {
  RpcSimpleResult result = {0};
  rpc_state.node_id = node_id;

  if (!connect_to_server()) {
    result.success = false;
    result.error_code = RPC_DISCONNECTED;
    return result;
  }

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_register_node_json(request, sizeof(request), node_id);

  if (!send_and_receive(request, response, sizeof(response))) {
    result.success = false;
    result.error_code = RPC_TIMEOUT;
    return result;
  }

  return parse_register_node_response(response);
}

RpcClickResult rpc_add_clicks(int clicks, int lamport_ts) {
  RpcClickResult result = {0};

  if (!connect_to_server()) {
    rpc_state.offline_queue.accumulated_clicks += clicks;
    rpc_state.offline_queue.last_lamport = lamport_ts;

    result.success = false;
    result.error_code = RPC_OFFLINE_QUEUED;
    return result;
  }

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_add_clicks_json(request, sizeof(request), clicks, lamport_ts);

  if (!send_and_receive(request, response, sizeof(response))) {
    rpc_state.offline_queue.accumulated_clicks += clicks;
    rpc_state.offline_queue.last_lamport = lamport_ts;

    result.success = false;
    result.error_code = RPC_DISCONNECTED;
    return result;
  }

  return parse_add_clicks_response(response);
}

RpcPowerupResult rpc_activate_powerup(void) {
  RpcPowerupResult result = {0};

  if (!connect_to_server()) {
    result.success = false;
    result.error_code = RPC_DISCONNECTED;
    return result;
  }

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_activate_powerup_json(request, sizeof(request));

  if (!send_and_receive(request, response, sizeof(response))) {
    result.success = false;
    result.error_code = RPC_TIMEOUT;
    return result;
  }

  return parse_activate_powerup_response(response);
}

RpcScoreResult rpc_get_scores(void) {
  RpcScoreResult result = {0};

  if (!connect_to_server()) {
    result.success = false;
    result.error_code = RPC_DISCONNECTED;
    return result;
  }

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_get_scores_json(request, sizeof(request));

  if (!send_and_receive(request, response, sizeof(response))) {
    result.success = false;
    result.error_code = RPC_TIMEOUT;
    return result;
  }

  return parse_get_scores_response(response);
}

RpcClickResult rpc_sync_offline(int accumulated_clicks, int lamport_ts) {
  RpcClickResult result = {0};

  if (!connect_to_server()) {
    result.success = false;
    result.error_code = RPC_DISCONNECTED;
    return result;
  }

  char request[256];
  char response[RPC_BUFFER_SIZE];
  build_sync_offline_json(request, sizeof(request), accumulated_clicks,
                          lamport_ts);

  if (!send_and_receive(request, response, sizeof(response))) {
    result.success = false;
    result.error_code = RPC_TIMEOUT;
    return result;
  }

  return parse_add_clicks_response(response);
}

void rpc_poll(void) {
  if (rpc_state.offline_queue.accumulated_clicks <= 0) {
    return;
  }

  uint32_t now = now_ms();
  if ((now - rpc_state.last_reconnect_attempt_ms) < RECONNECT_INTERVAL_MS) {
    return;
  }

  rpc_state.last_reconnect_attempt_ms = now;

  RpcClickResult result =
      rpc_sync_offline(rpc_state.offline_queue.accumulated_clicks,
                       rpc_state.offline_queue.last_lamport);
  if (result.success) {
    printf("[RPC] Fila drenada com sucesso (%d cliques)\n",
           rpc_state.offline_queue.accumulated_clicks);
    rpc_state.offline_queue.accumulated_clicks = 0;
    rpc_state.offline_queue.last_lamport = 0;
  }
}

bool rpc_is_connected(void) { return rpc_state.sock >= 0; }
