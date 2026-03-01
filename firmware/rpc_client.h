/**
 * @file rpc_client.h
 * @brief API pública de comunicação JSON-RPC com servidor
 *
 * Esta é a ÚNICA interface de rede exposta ao firmware.
 * Nenhum outro arquivo deve incluir lwip/sockets.h.
 */

#ifndef RPC_CLIENT_H
#define RPC_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Códigos de erro retornados pelas operações RPC.
 *
 * @note Campos não preenchidos nos resultados são inicializados com
 * zero(memset)
 */
typedef enum {
  RPC_OK = 0,            ///< Operação bem-sucedida
  RPC_TIMEOUT,           ///< Servidor não respondeu a tempo
  RPC_DISCONNECTED,      ///< Socket fechado, sem conexão
  RPC_LAMPORT_VIOLATION, ///< Servidor rejeitou por timestamp inválido
  RPC_RATE_EXCEEDED,     ///< Servidor limitou taxa de cliques
  RPC_OFFLINE_QUEUED,    ///< Offline, cliques enfileirados localmente
  RPC_PARSE_ERROR        ///< Resposta JSON mal formatada
} RpcError;

/**
 * Resultado de operações simples (init, register).
 */
typedef struct {
  bool success;        ///< true se operação bem-sucedida
  RpcError error_code; ///< Código de erro (RPC_OK se success)
} RpcSimpleResult;

/**
 * Resultado de rpc_add_clicks() e rpc_sync_offline().
 * Campos válidos apenas se success == true.
 */
typedef struct {
  bool success;
  RpcError error_code;

  int global_score;         ///< Score global de todos os nós
  int local_score;          ///< Score deste nó
  int lamport_ts;           ///< Timestamp Lamport atualizado
  bool milestone_triggered; ///< true se atingiu marco
  int milestone_value;      ///< Valor do marco (válido se milestone_triggered)
  int accepted_clicks;      ///< Cliques aceitos (pode ser < enviado se
                            ///< RPC_RATE_EXCEEDED)
} RpcClickResult;

/**
 * Resultado de rpc_activate_powerup().
 */
typedef struct {
  bool success;
  RpcError error_code;

  int powerup_remaining_s; ///< Segundos restantes de powerup (10s máx)
} RpcPowerupResult;

/**
 * Resultado de rpc_get_scores().
 */
typedef struct {
  bool success;
  RpcError error_code;

  int global_score;   ///< Score global
  int node_scores[3]; ///< Scores individuais [node_0, node_1, node_2]
} RpcScoreResult;

/**
 * Inicializa cliente RPC.
 * Deve ser chamado uma vez no boot, antes de qualquer outra função.
 * Cria task de reconexão automática em background.
 *
 * @return RpcSimpleResult com status da inicialização
 */
RpcSimpleResult rpc_init(void);

/**
 * Registra este nó no servidor.
 *
 * @param node_id ID do nó (0, 1 ou 2)
 * @return RpcSimpleResult - falha se servidor offline
 */
RpcSimpleResult rpc_register_node(uint8_t node_id);

/**
 * Envia cliques para o servidor.
 * Se offline, enfileira localmente e retorna RPC_OFFLINE_QUEUED.
 * Fila é sincronizada automaticamente quando servidor voltar.
 *
 * @param clicks Número de cliques a enviar
 * @param lamport_ts Timestamp Lamport local atual
 * @return RpcClickResult com scores atualizados
 */
RpcClickResult rpc_add_clicks(int clicks, int lamport_ts);

/**
 * Ativa power-up (multiplicador x3 por 10 segundos).
 *
 * @return RpcPowerupResult com tempo restante se já ativo
 */
RpcPowerupResult rpc_activate_powerup(void);

/**
 * Consulta scores de todos os nós.
 *
 * @return RpcScoreResult com placar global e individual
 */
RpcScoreResult rpc_get_scores(void);

/**
 * Sincroniza cliques acumulados durante período offline.
 * Chamada automaticamente pela task de reconexão.
 *
 * @param accumulated_clicks Total de cliques acumulados
 * @param lamport_ts Último Lamport conhecido
 * @return RpcClickResult com scores atualizados
 */
RpcClickResult rpc_sync_offline(int accumulated_clicks, int lamport_ts);

/**
 * Configura endereço do servidor RPC (descoberto via mDNS/service discovery).
 *
 * @param ip_str String com endereço IP (ex: "192.168.0.10")
 * @param port Porta do servidor (ex: 8765)
 *
 * Exemplo:
 *   rpc_client_set_server("192.168.0.10", 8765);
 */
void rpc_client_set_server(const char *ip_str, uint16_t port);

/**
 * Usa servidor fallback hardcoded (192.168.0.10:8765).
 * Chamado quando discovery falha.
 */
void rpc_client_set_server_fallback(void);

/**
 * Polling de reconexão e drenagem de fila offline.
 * Deve ser chamado periodicamente no loop principal.
 */
void rpc_poll(void);

/**
 * Verifica se está conectado ao servidor.
 *
 * @return true se conectado, false caso contrário
 */
bool rpc_is_connected(void);

#endif // RPC_CLIENT_H
